/*
**	Command & Conquer Renegade(tm)
**	Copyright 2026 OpenW3D Contributors.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Device and render system for the native D3D12 backend.

#include "w3drender_d3d12.h"

#include "wwdebug.h"

#include "g_ffp_vs.h"
#include "g_ffp_ps.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	constexpr unsigned MAX_STAGES = 2;		// matches ffp.hlsl and the engine's MAX_TEXTURE_STAGES
	constexpr unsigned MAX_LIGHTS = 4;

	enum RootParameter
	{
		ROOT_CONSTANTS,
		ROOT_TEXTURE0,
		ROOT_TEXTURE1,
		ROOT_SAMPLER0,
		ROOT_SAMPLER1,
		ROOT_COUNT
	};

	// Layout must match FFPConstants in ffp.hlsl.
	struct FFPLightCB
	{
		float		Diffuse[4];
		float		Specular[4];
		float		Ambient[4];
		float		Position[4];
		float		Direction[4];
		float		Range[4];
		float		Attenuation[4];
	};

	struct FFPConstantsCB
	{
		float		WorldView[16];
		float		NormalMatrix[16];
		float		Projection[16];
		float		TexTransform[MAX_STAGES][16];
		float		MaterialDiffuse[4];
		float		MaterialAmbient[4];
		float		MaterialSpecular[4];
		float		MaterialEmissive[4];
		float		MaterialPower[4];
		float		GlobalAmbient[4];
		float		FogColor[4];
		float		FogParams[4];
		float		TextureFactor[4];
		float		BumpEnvMatrix[4];
		float		BumpEnvLuminance[4];
		uint32_t	VertexFlags[4];
		uint32_t	StageColor[MAX_STAGES][4];
		uint32_t	StageAlpha[MAX_STAGES][4];
		uint32_t	StageCoords[MAX_STAGES][4];
		uint32_t	PixelFlags[4];
		FFPLightCB	Lights[MAX_LIGHTS];
	};
	static_assert(sizeof(FFPConstantsCB) % 16 == 0, "constant buffer must be a whole number of registers");

	// Constant data for vertex elements the current FVF does not provide.
	struct DefaultVertex
	{
		float		Normal[3];		// 0
		float		Pad0;
		uint32_t	Diffuse;			// 16: white
		uint32_t	Specular;		// 20: black
		float		Pad1[2];
		float		Texcoord[4];	// 32
	};

	const D3DMATRIX IDENTITY = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };

	D3DMATRIX Multiply(const D3DMATRIX &a, const D3DMATRIX &b)
	{
		D3DMATRIX r;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
			}
		}
		return r;
	}

	// Inverse transpose of the upper 3x3, for transforming normals as row vectors.
	D3DMATRIX Normal_Matrix(const D3DMATRIX &m)
	{
		const float a = m._11, b = m._12, c = m._13;
		const float d = m._21, e = m._22, f = m._23;
		const float g = m._31, h = m._32, i = m._33;
		const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
		const float det = a * A + b * B + c * C;
		D3DMATRIX r = IDENTITY;
		if (std::fabs(det) < 1e-12f) {
			r._11 = a; r._12 = b; r._13 = c;
			r._21 = d; r._22 = e; r._23 = f;
			r._31 = g; r._32 = h; r._33 = i;
			return r;
		}
		const float inv = 1.0f / det;
		// transpose(inverse(M)) is the cofactor matrix divided by the determinant.
		r._11 = A * inv;                 r._12 = B * inv;                 r._13 = C * inv;
		r._21 = -(b * i - c * h) * inv;  r._22 = (a * i - c * g) * inv;   r._23 = -(a * h - b * g) * inv;
		r._31 = (b * f - c * e) * inv;   r._32 = -(a * f - c * d) * inv;  r._33 = (a * e - b * d) * inv;
		return r;
	}

	void Copy_Matrix(float out[16], const D3DMATRIX &m)
	{
		memcpy(out, &m, sizeof(float) * 16);
	}

	void Copy_Color(float out[4], const D3DCOLORVALUE &c)
	{
		out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
	}

	void Unpack_Color(float out[4], DWORD c)
	{
		out[0] = float((c >> 16) & 0xff) / 255.0f;
		out[1] = float((c >> 8) & 0xff) / 255.0f;
		out[2] = float(c & 0xff) / 255.0f;
		out[3] = float((c >> 24) & 0xff) / 255.0f;
	}

	float As_Float(DWORD value)
	{
		float f;
		memcpy(&f, &value, sizeof(f));
		return f;
	}

	void Transform_Point(const D3DMATRIX &m, const D3DVECTOR &v, float out[4])
	{
		out[0] = v.x * m._11 + v.y * m._21 + v.z * m._31 + m._41;
		out[1] = v.x * m._12 + v.y * m._22 + v.z * m._32 + m._42;
		out[2] = v.x * m._13 + v.y * m._23 + v.z * m._33 + m._43;
		out[3] = 1.0f;
	}

	void Transform_Direction(const D3DMATRIX &m, const D3DVECTOR &v, float out[4])
	{
		float x = v.x * m._11 + v.y * m._21 + v.z * m._31;
		float y = v.x * m._12 + v.y * m._22 + v.z * m._32;
		float z = v.x * m._13 + v.y * m._23 + v.z * m._33;
		const float len = std::sqrt(x * x + y * y + z * z);
		if (len > 0.0f) {
			x /= len; y /= len; z /= len;
		}
		out[0] = x; out[1] = y; out[2] = z; out[3] = 0.0f;
	}

	D3D12_BLEND Alpha_Blend(D3D12_BLEND blend)
	{
		// Colour factors are not valid for the alpha channel; D3D9 uses their alpha.
		switch (blend) {
		case D3D12_BLEND_SRC_COLOR:			return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR:		return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR:			return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR:		return D3D12_BLEND_INV_DEST_ALPHA;
		default:										return blend;
		}
	}

	unsigned Texcoord_Size(DWORD fvf, unsigned index)
	{
		switch ((fvf >> (16 + index * 2)) & 3) {
		case D3DFVF_TEXTUREFORMAT1: return 1;
		case D3DFVF_TEXTUREFORMAT3: return 3;
		case D3DFVF_TEXTUREFORMAT4: return 4;
		default:                    return 2;
		}
	}

	DXGI_FORMAT Float_Format(unsigned count)
	{
		switch (count) {
		case 1:  return DXGI_FORMAT_R32_FLOAT;
		case 3:  return DXGI_FORMAT_R32G32B32_FLOAT;
		case 4:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default: return DXGI_FORMAT_R32G32_FLOAT;
		}
	}

	std::string Narrow(const wchar_t *text)
	{
		char buffer[512] = {};
		WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr);
		return buffer;
	}

	void Fill_Caps(D3DCAPS9 &caps, UINT adapter)
	{
		memset(&caps, 0, sizeof(caps));
		caps.DeviceType = D3DDEVTYPE_HAL;
		caps.AdapterOrdinal = adapter;
		caps.Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANCALIBRATEGAMMA | D3DCAPS2_DYNAMICTEXTURES;
		caps.PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_TWO;
		caps.DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_DRAWPRIMITIVES2EX |
			D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY;
		caps.PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
			D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_MASKZ;
		caps.RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGRANGE |
			D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_DEPTHBIAS |
			D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_SCISSORTEST;
		caps.ZCmpCaps = 0xff;
		caps.AlphaCmpCaps = 0xff;
		caps.SrcBlendCaps = 0x1fff;
		caps.DestBlendCaps = 0x1fff;
		caps.ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
			D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
		caps.TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_PERSPECTIVE |
			D3DPTEXTURECAPS_PROJECTED | D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE;
		caps.TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
			D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR |
			D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
		caps.TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
			D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_MIRRORONCE | D3DPTADDRESSCAPS_INDEPENDENTUV;
		caps.MaxTextureWidth = 16384;
		caps.MaxTextureHeight = 16384;
		caps.MaxTextureRepeat = 8192;
		caps.MaxTextureAspectRatio = 16384;
		caps.MaxAnisotropy = 16;
		caps.MaxVertexW = 1e10f;
		caps.FVFCaps = 8;
		caps.TextureOpCaps = 0x03ffffff;
		caps.MaxTextureBlendStages = MAX_STAGES;
		caps.MaxSimultaneousTextures = MAX_STAGES;
		caps.VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
			D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
		caps.MaxActiveLights = MAX_LIGHTS;
		caps.MaxPrimitiveCount = 0xfffff;
		caps.MaxVertexIndex = 0xffffff;
		caps.MaxStreams = 1;
		caps.MaxStreamStride = 255;
		caps.VertexShaderVersion = D3DVS_VERSION(3, 0);
		caps.PixelShaderVersion = D3DPS_VERSION(3, 0);
		caps.MaxPointSize = 1.0f;
		caps.NumSimultaneousRTs = 1;
		caps.MasterAdapterOrdinal = adapter;
		caps.NumberOfAdaptersInGroup = 1;
	}

	bool Write_TGA(const char *filename, const std::vector<uint32_t> &pixels, UINT width, UINT height)
	{
		FILE *file = fopen(filename, "wb");
		if (file == nullptr) {
			return false;
		}
		uint8_t header[18] = {};
		header[2] = 2;							// uncompressed true colour
		header[12] = uint8_t(width);
		header[13] = uint8_t(width >> 8);
		header[14] = uint8_t(height);
		header[15] = uint8_t(height >> 8);
		header[16] = 32;
		header[17] = 0x28;					// 8 alpha bits, top-left origin
		bool ok = fwrite(header, sizeof(header), 1, file) == 1;
		ok = ok && fwrite(pixels.data(), 4, pixels.size(), file) == pixels.size();
		fclose(file);
		return ok;
	}
}

/***********************************************************************************************
** D3D12RenderDevice
***********************************************************************************************/

D3D12RenderDevice::D3D12RenderDevice()
{
	memset(Textures, 0, sizeof(Textures));
	Reset_State();
}

D3D12RenderDevice::~D3D12RenderDevice()
{
	for (D3D12Texture *&texture : Textures) {
		if (texture) {
			texture->Release();
			texture = nullptr;
		}
	}
	if (Stream) Stream->Release();
	if (Indices) Indices->Release();
	if (RenderTarget) RenderTarget->Release();
	if (WhiteTexture) WhiteTexture->Release();
	if (BackBufferSurface) BackBufferSurface->Release();
	if (DepthSurface) DepthSurface->Release();
	Pipelines.clear();
	RootSignature.Reset();
	DefaultVertexData.Reset();
	Native_Device.Shutdown();
}

void D3D12RenderDevice::Reset_State()
{
	memset(RenderStates, 0, sizeof(RenderStates));
	RenderStates[D3DRS_ZENABLE] = D3DZB_TRUE;
	RenderStates[D3DRS_FILLMODE] = D3DFILL_SOLID;
	RenderStates[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	RenderStates[D3DRS_ZWRITEENABLE] = TRUE;
	RenderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	RenderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	RenderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
	RenderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	RenderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	RenderStates[D3DRS_FOGEND] = 0x3f800000;		// 1.0f
	RenderStates[D3DRS_FOGDENSITY] = 0x3f800000;
	RenderStates[D3DRS_TEXTUREFACTOR] = 0xffffffff;
	RenderStates[D3DRS_LIGHTING] = TRUE;
	RenderStates[D3DRS_COLORVERTEX] = TRUE;
	RenderStates[D3DRS_LOCALVIEWER] = TRUE;
	RenderStates[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
	RenderStates[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
	RenderStates[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
	RenderStates[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
	RenderStates[D3DRS_COLORWRITEENABLE] = 0xf;
	RenderStates[D3DRS_BLENDOP] = D3DBLENDOP_ADD;

	memset(StageStates, 0, sizeof(StageStates));
	for (unsigned stage = 0; stage < 8; ++stage) {
		DWORD *s = StageStates[stage];
		s[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		s[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		s[D3DTSS_COLORARG2] = D3DTA_CURRENT;
		s[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		s[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		s[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		s[D3DTSS_TEXCOORDINDEX] = stage;
		s[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
		s[D3DTSS_COLORARG0] = D3DTA_CURRENT;
		s[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
		s[D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}

	memset(SamplerStates, 0, sizeof(SamplerStates));
	for (unsigned sampler = 0; sampler < 8; ++sampler) {
		DWORD *s = SamplerStates[sampler];
		s[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
		s[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
		s[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
		s[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
		s[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
		s[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
		s[D3DSAMP_MAXANISOTROPY] = 1;
	}

	World = View = Projection = IDENTITY;
	for (D3DMATRIX &m : TextureMatrices) {
		m = IDENTITY;
	}
	memset(&Material, 0, sizeof(Material));
	memset(Lights, 0, sizeof(Lights));
	memset(LightEnabled, 0, sizeof(LightEnabled));
	for (D3DLIGHT9 &light : Lights) {
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
		light.Direction.z = 1.0f;
	}

	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Native_Device.Is_Initialized() ? Native_Device.Width() : 1;
	Viewport.Height = Native_Device.Is_Initialized() ? Native_Device.Height() : 1;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;

	for (D3D12Texture *&texture : Textures) {
		if (texture) {
			texture->Release();
			texture = nullptr;
		}
	}
	if (Stream) { Stream->Release(); Stream = nullptr; }
	if (Indices) { Indices->Release(); Indices = nullptr; }
	if (RenderTarget) { RenderTarget->Release(); RenderTarget = nullptr; }
	StreamOffset = 0;
	StreamStride = 0;
	FVF = 0;
	TargetsDirty = true;
}

bool D3D12RenderDevice::Init(IDXGIAdapter1 *adapter, HWND window, const D3DPRESENT_PARAMETERS &params)
{
	D3D12DeviceInitParams init;
	init.Window = params.hDeviceWindow ? params.hDeviceWindow : window;
	init.Adapter = adapter;
	init.Width = params.BackBufferWidth;
	init.Height = params.BackBufferHeight;
	if ((init.Width == 0 || init.Height == 0) && init.Window != nullptr) {
		RECT rect = {};
		GetClientRect(init.Window, &rect);
		init.Width = UINT(rect.right - rect.left);
		init.Height = UINT(rect.bottom - rect.top);
	}
	const char *debug = std::getenv("W3D_D3D12_DEBUG");
	init.DebugLayer = debug != nullptr && debug[0] == '1';

	if (!Native_Device.Init(init) || !Create_Root_Signature() || !Create_Defaults()) {
		return false;
	}

	switch (params.PresentationInterval) {
	case D3DPRESENT_INTERVAL_IMMEDIATE:	SyncInterval = 0; break;
	case D3DPRESENT_INTERVAL_TWO:			SyncInterval = 2; break;
	case D3DPRESENT_INTERVAL_THREE:		SyncInterval = 3; break;
	default:										SyncInterval = 1; break;
	}
	const char *stats = std::getenv("W3D_RENDER_STATS");
	StatsEnabled = stats != nullptr && stats[0] == '1';

	BackBufferSurface = new D3D12Surface(this, D3D12Surface::KIND_BACK_BUFFER);
	DepthSurface = new D3D12Surface(this, D3D12Surface::KIND_DEPTH);
	Reset_State();
	DepthStencil = params.EnableAutoDepthStencil ? DepthSurface : nullptr;
	return true;
}

bool D3D12RenderDevice::Create_Root_Signature()
{
	D3D12_DESCRIPTOR_RANGE ranges[4] = {};
	for (unsigned i = 0; i < 2; ++i) {
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i;
		ranges[i + 2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		ranges[i + 2].NumDescriptors = 1;
		ranges[i + 2].BaseShaderRegister = i;
	}

	D3D12_ROOT_PARAMETER params[ROOT_COUNT] = {};
	params[ROOT_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[ROOT_CONSTANTS].Descriptor.ShaderRegister = 0;
	params[ROOT_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	for (unsigned i = 0; i < 4; ++i) {
		D3D12_ROOT_PARAMETER &p = params[ROOT_TEXTURE0 + i];
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		p.DescriptorTable.NumDescriptorRanges = 1;
		p.DescriptorTable.pDescriptorRanges = &ranges[i];
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = ROOT_COUNT;
	desc.pParameters = params;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> blob, error;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if (FAILED(hr)) {
		WWRELEASE_WARNING(("D3D12: root signature serialisation failed: %s\n", error ? (const char *)error->GetBufferPointer() : ""));
		return false;
	}
	hr = Native_Device.Device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&RootSignature));
	return SUCCEEDED(hr);
}

bool D3D12RenderDevice::Create_Defaults()
{
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = 256;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(Native_Device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&DefaultVertexData)))) {
		return false;
	}
	DefaultVertex defaults = {};
	defaults.Diffuse = 0xffffffff;
	defaults.Texcoord[3] = 1.0f;
	void *mapped = nullptr;
	const D3D12_RANGE no_read = { 0, 0 };
	if (FAILED(DefaultVertexData->Map(0, &no_read, &mapped))) {
		return false;
	}
	memcpy(mapped, &defaults, sizeof(defaults));
	DefaultVertexData->Unmap(0, nullptr);

	// Stages with no texture read opaque white, so texture arguments are neutral.
	WhiteTexture = new D3D12Texture(this, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, false);
	if (!WhiteTexture->Create_GPU_Resource()) {
		return false;
	}
	D3DLOCKED_RECT locked;
	WhiteTexture->LockRect(0, &locked, nullptr, 0);
	*static_cast<uint32_t *>(locked.pBits) = 0xffffffff;
	WhiteTexture->UnlockRect(0);
	return true;
}

void D3D12RenderDevice::Ensure_Frame()
{
	if (!Native_Device.Is_In_Frame()) {
		Native_Device.Begin_Frame();
		TargetsDirty = true;
	}
	if (BoundEpoch != Native_Device.List_Epoch()) {
		BoundEpoch = Native_Device.List_Epoch();
		TargetsDirty = true;
	}
}

void D3D12RenderDevice::Bind_Targets()
{
	ID3D12GraphicsCommandList *list = Native_Device.Command_List();
	D3D12_CPU_DESCRIPTOR_HANDLE rtv;
	UINT width = Native_Device.Width();
	UINT height = Native_Device.Height();
	if (RenderTarget != nullptr) {
		rtv = RenderTarget->Texture()->Prepare_For_Rendering();
		width = RenderTarget->Width();
		height = RenderTarget->Height();
	} else {
		rtv = Native_Device.Back_Buffer_RTV();
	}

	// The shared depth buffer only fits targets of the same size.
	const bool depth = DepthStencil != nullptr && width <= Native_Device.Width() && height <= Native_Device.Height();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = Native_Device.Depth_DSV();
	list->OMSetRenderTargets(1, &rtv, FALSE, depth ? &dsv : nullptr);
	TargetsDirty = false;
}

ID3D12PipelineState *D3D12RenderDevice::Get_Pipeline(const D3D12PipelineKey &key)
{
	for (const Pipeline &pipeline : Pipelines) {
		if (pipeline.Key == key) {
			return pipeline.State.Get();
		}
	}

	// Input layout for the FVF. Missing elements read constants from input slot 1.
	D3D12_INPUT_ELEMENT_DESC elements[12] = {};
	UINT count = 0;
	UINT offset = 0;
	auto add = [&](const char *name, UINT index, DXGI_FORMAT format, bool present, UINT size, UINT default_offset) {
		D3D12_INPUT_ELEMENT_DESC &e = elements[count++];
		e.SemanticName = name;
		e.SemanticIndex = index;
		e.Format = format;
		e.InputSlot = present ? 0 : 1;
		e.AlignedByteOffset = present ? offset : default_offset;
		e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		if (present) {
			offset += size;
		}
	};

	const DWORD fvf = key.FVF;
	add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, true, 12, 0);
	add("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, (fvf & D3DFVF_NORMAL) != 0, 12, offsetof(DefaultVertex, Normal));
	if (fvf & D3DFVF_PSIZE) {
		offset += 4;
	}
	add("COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, (fvf & D3DFVF_DIFFUSE) != 0, 4, offsetof(DefaultVertex, Diffuse));
	add("COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, (fvf & D3DFVF_SPECULAR) != 0, 4, offsetof(DefaultVertex, Specular));
	const unsigned tex_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (unsigned i = 0; i < 8; ++i) {
		const unsigned size = Texcoord_Size(fvf, i);
		const bool present = i < tex_count;
		add("TEXCOORD", i, present ? Float_Format(size) : DXGI_FORMAT_R32G32B32A32_FLOAT, present, size * 4, offsetof(DefaultVertex, Texcoord));
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = RootSignature.Get();
	desc.VS = { g_ffp_vs, sizeof(g_ffp_vs) };
	desc.PS = { g_ffp_ps, sizeof(g_ffp_ps) };

	D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
	blend.BlendEnable = key.BlendEnable;
	blend.SrcBlend = D3D12_BLEND(key.SrcBlend);
	blend.DestBlend = D3D12_BLEND(key.DstBlend);
	blend.BlendOp = D3D12_BLEND_OP_ADD;
	blend.SrcBlendAlpha = Alpha_Blend(blend.SrcBlend);
	blend.DestBlendAlpha = Alpha_Blend(blend.DestBlend);
	blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blend.LogicOp = D3D12_LOGIC_OP_NOOP;
	blend.RenderTargetWriteMask = key.ColorWrite;
	desc.SampleMask = UINT_MAX;

	desc.RasterizerState.FillMode = key.Fill == D3DFILL_SOLID ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
	switch (key.Cull) {
	case D3DCULL_CW:	desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
	case D3DCULL_CCW:	desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
	default:				desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
	}
	desc.RasterizerState.FrontCounterClockwise = FALSE;	// D3D9: clockwise triangles face the viewer
	desc.RasterizerState.DepthBias = key.DepthBias;
	desc.RasterizerState.SlopeScaledDepthBias = key.SlopeBias;
	desc.RasterizerState.DepthClipEnable = TRUE;

	desc.DepthStencilState.DepthEnable = key.HasDepth && key.DepthEnable;
	desc.DepthStencilState.DepthWriteMask = key.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(key.DepthFunc);

	desc.InputLayout = { elements, count };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT(key.RTFormat);
	desc.DSVFormat = key.HasDepth ? Native_Device.Depth_Format() : DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;

	Pipeline pipeline;
	pipeline.Key = key;
	const HRESULT hr = Native_Device.Device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline.State));
	if (FAILED(hr)) {
		// Remember the failure so a broken key does not retry (and log) every draw.
		WWRELEASE_WARNING(("D3D12: pipeline creation failed for FVF 0x%x: %s\n", unsigned(fvf), D3D12_Result_Name(hr)));
		Pipelines.push_back(pipeline);
		return nullptr;
	}
	Pipelines.push_back(pipeline);
	return Pipelines.back().State.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12RenderDevice::Get_Sampler(unsigned stage)
{
	const DWORD *s = SamplerStates[stage];
	const DWORD min = s[D3DSAMP_MINFILTER], mag = s[D3DSAMP_MAGFILTER], mip = s[D3DSAMP_MIPFILTER];
	const DWORD aniso = std::clamp<DWORD>(s[D3DSAMP_MAXANISOTROPY], 1, 16);
	const uint64_t key = uint64_t(min) | (uint64_t(mag) << 4) | (uint64_t(mip) << 8) |
		(uint64_t(s[D3DSAMP_ADDRESSU]) << 12) | (uint64_t(s[D3DSAMP_ADDRESSV]) << 16) |
		(uint64_t(s[D3DSAMP_ADDRESSW]) << 20) | (uint64_t(aniso) << 24);

	D3D12DescriptorHeapClass &heap = Native_Device.Sampler_Heap();
	for (const SamplerEntry &entry : Samplers) {
		if (entry.Key == key) {
			return heap.GPU(entry.Index);
		}
	}

	D3D12_SAMPLER_DESC desc = {};
	if (min == D3DTEXF_ANISOTROPIC || mag == D3DTEXF_ANISOTROPIC) {
		desc.Filter = D3D12_FILTER_ANISOTROPIC;
		desc.MaxAnisotropy = aniso;
	} else {
		const auto type = [](DWORD f) { return f == D3DTEXF_POINT || f == D3DTEXF_NONE ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR; };
		desc.Filter = D3D12_FILTER(D3D12_ENCODE_BASIC_FILTER(type(min), type(mag), type(mip), D3D12_FILTER_REDUCTION_TYPE_STANDARD));
		desc.MaxAnisotropy = 1;
	}
	const auto address = [](DWORD mode) {
		return (mode >= D3DTADDRESS_WRAP && mode <= D3DTADDRESS_MIRRORONCE) ? D3D12_TEXTURE_ADDRESS_MODE(mode) : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	};
	desc.AddressU = address(s[D3DSAMP_ADDRESSU]);
	desc.AddressV = address(s[D3DSAMP_ADDRESSV]);
	desc.AddressW = address(s[D3DSAMP_ADDRESSW]);
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.MinLOD = 0.0f;
	desc.MaxLOD = (mip == D3DTEXF_NONE) ? 0.0f : FLT_MAX;	// no mip filter: base level only

	const unsigned index = heap.Allocate();
	if (index == UINT32_MAX) {
		return heap.GPU(Samplers.empty() ? 0 : Samplers.front().Index);
	}
	Native_Device.Device()->CreateSampler(&desc, heap.CPU(index));
	Samplers.push_back({ key, index });
	return heap.GPU(index);
}

void D3D12RenderDevice::Fill_Constants(void *data)
{
	FFPConstantsCB &cb = *static_cast<FFPConstantsCB *>(data);
	memset(&cb, 0, sizeof(cb));

	const D3DMATRIX world_view = Multiply(World, View);
	Copy_Matrix(cb.WorldView, world_view);
	Copy_Matrix(cb.NormalMatrix, Normal_Matrix(world_view));
	Copy_Matrix(cb.Projection, Projection);
	for (unsigned stage = 0; stage < MAX_STAGES; ++stage) {
		Copy_Matrix(cb.TexTransform[stage], TextureMatrices[stage]);
	}

	Copy_Color(cb.MaterialDiffuse, Material.Diffuse);
	Copy_Color(cb.MaterialAmbient, Material.Ambient);
	Copy_Color(cb.MaterialSpecular, Material.Specular);
	Copy_Color(cb.MaterialEmissive, Material.Emissive);
	cb.MaterialPower[0] = Material.Power;
	Unpack_Color(cb.GlobalAmbient, RenderStates[D3DRS_AMBIENT]);
	Unpack_Color(cb.FogColor, RenderStates[D3DRS_FOGCOLOR]);
	Unpack_Color(cb.TextureFactor, RenderStates[D3DRS_TEXTUREFACTOR]);

	const float fog_start = As_Float(RenderStates[D3DRS_FOGSTART]);
	const float fog_end = As_Float(RenderStates[D3DRS_FOGEND]);
	cb.FogParams[0] = fog_start;
	cb.FogParams[1] = fog_end;
	cb.FogParams[2] = (fog_end != fog_start) ? 1.0f / (fog_end - fog_start) : 0.0f;

	cb.BumpEnvMatrix[0] = As_Float(StageStates[0][D3DTSS_BUMPENVMAT00]);
	cb.BumpEnvMatrix[1] = As_Float(StageStates[0][D3DTSS_BUMPENVMAT01]);
	cb.BumpEnvMatrix[2] = As_Float(StageStates[0][D3DTSS_BUMPENVMAT10]);
	cb.BumpEnvMatrix[3] = As_Float(StageStates[0][D3DTSS_BUMPENVMAT11]);
	cb.BumpEnvLuminance[0] = As_Float(StageStates[0][D3DTSS_BUMPENVLSCALE]);
	cb.BumpEnvLuminance[1] = As_Float(StageStates[0][D3DTSS_BUMPENVLOFFSET]);

	cb.VertexFlags[0] = ((FVF & D3DFVF_NORMAL) ? 0x1u : 0u) | ((FVF & D3DFVF_DIFFUSE) ? 0x2u : 0u) | ((FVF & D3DFVF_SPECULAR) ? 0x4u : 0u);
	cb.VertexFlags[1] = (RenderStates[D3DRS_LIGHTING] ? 0x1u : 0u) | (RenderStates[D3DRS_COLORVERTEX] ? 0x2u : 0u) |
		(RenderStates[D3DRS_SPECULARENABLE] ? 0x4u : 0u) | (RenderStates[D3DRS_LOCALVIEWER] ? 0x8u : 0u);
	cb.VertexFlags[2] = (RenderStates[D3DRS_AMBIENTMATERIALSOURCE] & 3) | ((RenderStates[D3DRS_DIFFUSEMATERIALSOURCE] & 3) << 2) |
		((RenderStates[D3DRS_SPECULARMATERIALSOURCE] & 3) << 4) | ((RenderStates[D3DRS_EMISSIVEMATERIALSOURCE] & 3) << 6);
	const bool fog = RenderStates[D3DRS_FOGENABLE] &&
		(RenderStates[D3DRS_FOGVERTEXMODE] != D3DFOG_NONE || RenderStates[D3DRS_FOGTABLEMODE] != D3DFOG_NONE);
	cb.VertexFlags[3] = (fog ? 0x1u : 0u) | (RenderStates[D3DRS_RANGEFOGENABLE] ? 0x2u : 0u);

	for (unsigned stage = 0; stage < MAX_STAGES; ++stage) {
		const DWORD *s = StageStates[stage];
		cb.StageColor[stage][0] = s[D3DTSS_COLOROP];
		cb.StageColor[stage][1] = s[D3DTSS_COLORARG1];
		cb.StageColor[stage][2] = s[D3DTSS_COLORARG2];
		cb.StageColor[stage][3] = s[D3DTSS_COLORARG0];
		cb.StageAlpha[stage][0] = s[D3DTSS_ALPHAOP];
		cb.StageAlpha[stage][1] = s[D3DTSS_ALPHAARG1];
		cb.StageAlpha[stage][2] = s[D3DTSS_ALPHAARG2];
		cb.StageAlpha[stage][3] = s[D3DTSS_ALPHAARG0];
		cb.StageCoords[stage][0] = s[D3DTSS_TEXCOORDINDEX];
		cb.StageCoords[stage][1] = s[D3DTSS_TEXTURETRANSFORMFLAGS];
		cb.StageCoords[stage][2] = (s[D3DTSS_RESULTARG] == D3DTA_TEMP) ? 1u : 0u;
	}

	if (RenderStates[D3DRS_ALPHATESTENABLE]) {
		cb.PixelFlags[0] = RenderStates[D3DRS_ALPHAFUNC];
		cb.PixelFlags[1] = RenderStates[D3DRS_ALPHAREF] & 0xff;
	}

	// Lights are given in world space; the shader lights in view space.
	unsigned slot = 0;
	for (unsigned i = 0; i < 8 && slot < MAX_LIGHTS; ++i) {
		if (!LightEnabled[i]) {
			continue;
		}
		const D3DLIGHT9 &light = Lights[i];
		FFPLightCB &out = cb.Lights[slot++];
		Copy_Color(out.Diffuse, light.Diffuse);
		Copy_Color(out.Specular, light.Specular);
		Copy_Color(out.Ambient, light.Ambient);
		Transform_Point(View, light.Position, out.Position);
		Transform_Direction(View, light.Direction, out.Direction);
		out.Range[0] = light.Range > 0.0f ? light.Range : FLT_MAX;
		out.Range[1] = light.Falloff;
		out.Range[2] = std::cos(light.Theta * 0.5f);
		out.Range[3] = std::cos(light.Phi * 0.5f);
		out.Attenuation[0] = light.Attenuation0;
		out.Attenuation[1] = light.Attenuation1;
		out.Attenuation[2] = light.Attenuation2;
		out.Attenuation[3] = float(light.Type);
	}
}

bool D3D12RenderDevice::Bind_Geometry(INT &base_vertex, UINT min_index, UINT vertex_count, UINT &start_index, UINT index_count)
{
	ID3D12GraphicsCommandList *list = Native_Device.Command_List();
	D3D12UploadRingClass &ring = Native_Device.Upload();

	D3D12_VERTEX_BUFFER_VIEW views[2] = {};
	const D3D12Buffer &vb = Stream->Buffer;
	if (vb.Is_Dynamic()) {
		// Snapshot just the vertices this draw reads.
		const INT64 first = INT64(base_vertex) + min_index;
		const UINT64 begin = StreamOffset + UINT64(std::max<INT64>(first, 0)) * StreamStride;
		const UINT64 bytes = UINT64(vertex_count) * StreamStride;
		if (first < 0 || begin + bytes > vb.Size() || bytes == 0) {
			return false;
		}
		D3D12UploadRingClass::Allocation staging;
		if (!ring.Allocate(bytes, 16, staging)) {
			return false;
		}
		memcpy(staging.CPU, vb.Data() + begin, size_t(bytes));
		views[0].BufferLocation = staging.GPU;
		views[0].SizeInBytes = UINT(bytes);
		base_vertex = -INT(min_index);
	} else {
		views[0].BufferLocation = vb.GPU_Address() + StreamOffset;
		views[0].SizeInBytes = vb.Size() - StreamOffset;
	}
	views[0].StrideInBytes = StreamStride;
	views[1].BufferLocation = DefaultVertexData->GetGPUVirtualAddress();
	views[1].SizeInBytes = 256;
	views[1].StrideInBytes = 0;
	list->IASetVertexBuffers(0, 2, views);

	const D3D12Buffer &ib = Indices->Buffer;
	const UINT index_size = (Indices->Format == D3DFMT_INDEX32) ? 4 : 2;
	D3D12_INDEX_BUFFER_VIEW ib_view = {};
	ib_view.Format = (index_size == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	if (ib.Is_Dynamic()) {
		const UINT64 begin = UINT64(start_index) * index_size;
		const UINT64 bytes = UINT64(index_count) * index_size;
		if (begin + bytes > ib.Size()) {
			return false;
		}
		D3D12UploadRingClass::Allocation staging;
		if (!ring.Allocate(bytes, 16, staging)) {
			return false;
		}
		memcpy(staging.CPU, ib.Data() + begin, size_t(bytes));
		ib_view.BufferLocation = staging.GPU;
		ib_view.SizeInBytes = UINT(bytes);
		start_index = 0;
	} else {
		ib_view.BufferLocation = ib.GPU_Address();
		ib_view.SizeInBytes = ib.Size();
	}
	list->IASetIndexBuffer(&ib_view);
	return true;
}

HRESULT D3D12RenderDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count)
{
	if (!StatsEnabled) {
		return Draw_Indexed(type, base_vertex, min_index, vertex_count, start_index, primitive_count);
	}
	LARGE_INTEGER begin, end;
	QueryPerformanceCounter(&begin);
	const HRESULT hr = Draw_Indexed(type, base_vertex, min_index, vertex_count, start_index, primitive_count);
	QueryPerformanceCounter(&end);
	Stats.DrawTicks += end.QuadPart - begin.QuadPart;
	return hr;
}

HRESULT D3D12RenderDevice::Draw_Indexed(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count)
{
	if (Stream == nullptr || Indices == nullptr || primitive_count == 0 || StreamStride == 0 || Native_Device.Is_Lost()) {
		return D3D_OK;
	}

	UINT index_count;
	D3D_PRIMITIVE_TOPOLOGY topology;
	switch (type) {
	case D3DPT_TRIANGLELIST:	index_count = primitive_count * 3; topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
	case D3DPT_TRIANGLESTRIP:	index_count = primitive_count + 2; topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
	default:
		WWDEBUG_WARNING(("D3D12: unsupported primitive type %d\n", int(type)));
		return D3DERR_INVALIDCALL;
	}

	Ensure_Frame();

	// Resolve textures first: uploads and state transitions must precede the draw.
	D3D12_GPU_DESCRIPTOR_HANDLE srv[MAX_STAGES];
	for (unsigned stage = 0; stage < MAX_STAGES; ++stage) {
		D3D12Texture *texture = Textures[stage];
		if (texture == nullptr || (RenderTarget != nullptr && texture == RenderTarget->Texture())) {
			texture = WhiteTexture;
		}
		srv[stage] = texture->Prepare_For_Sampling();
		if (srv[stage].ptr == 0) {
			srv[stage] = WhiteTexture->Prepare_For_Sampling();
		}
	}
	if (TargetsDirty) {
		Bind_Targets();
	}

	const UINT target_width = RenderTarget ? RenderTarget->Width() : Native_Device.Width();
	const UINT target_height = RenderTarget ? RenderTarget->Height() : Native_Device.Height();

	D3D12PipelineKey key;
	memset(&key, 0, sizeof(key));
	key.FVF = FVF;
	key.BlendEnable = RenderStates[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
	key.SrcBlend = key.BlendEnable ? uint8_t(RenderStates[D3DRS_SRCBLEND]) : uint8_t(D3DBLEND_ONE);
	key.DstBlend = key.BlendEnable ? uint8_t(RenderStates[D3DRS_DESTBLEND]) : uint8_t(D3DBLEND_ZERO);
	key.ColorWrite = uint8_t(RenderStates[D3DRS_COLORWRITEENABLE] & 0xf);
	key.HasDepth = (DepthStencil != nullptr && target_width <= Native_Device.Width() && target_height <= Native_Device.Height()) ? 1 : 0;
	key.DepthEnable = RenderStates[D3DRS_ZENABLE] ? 1 : 0;
	key.DepthWrite = RenderStates[D3DRS_ZWRITEENABLE] ? 1 : 0;
	key.DepthFunc = uint8_t(std::clamp<DWORD>(RenderStates[D3DRS_ZFUNC], D3DCMP_NEVER, D3DCMP_ALWAYS));
	key.Cull = uint8_t(RenderStates[D3DRS_CULLMODE]);
	key.Fill = uint8_t(RenderStates[D3DRS_FILLMODE]);
	key.DepthBias = INT(std::lround(As_Float(RenderStates[D3DRS_DEPTHBIAS]) * 16777216.0f));
	key.SlopeBias = As_Float(RenderStates[D3DRS_SLOPESCALEDEPTHBIAS]);
	key.RTFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	if (!key.HasDepth) {
		key.DepthEnable = key.DepthWrite = 0;
		key.DepthFunc = D3DCMP_ALWAYS;
	}

	ID3D12PipelineState *pso = Get_Pipeline(key);
	if (pso == nullptr) {
		return D3DERR_INVALIDCALL;
	}

	D3D12UploadRingClass::Allocation constants;
	if (!Native_Device.Upload().Allocate(sizeof(FFPConstantsCB), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants)) {
		return E_OUTOFMEMORY;
	}
	Fill_Constants(constants.CPU);

	if (!Bind_Geometry(base_vertex, min_index, vertex_count, start_index, index_count)) {
		WWDEBUG_WARNING(("D3D12: draw skipped, buffer range out of bounds\n"));
		return D3DERR_INVALIDCALL;
	}

	ID3D12GraphicsCommandList *list = Native_Device.Command_List();
	list->SetGraphicsRootSignature(RootSignature.Get());
	list->SetPipelineState(pso);
	list->SetGraphicsRootConstantBufferView(ROOT_CONSTANTS, constants.GPU);
	list->SetGraphicsRootDescriptorTable(ROOT_TEXTURE0, srv[0]);
	list->SetGraphicsRootDescriptorTable(ROOT_TEXTURE1, srv[1]);
	list->SetGraphicsRootDescriptorTable(ROOT_SAMPLER0, Get_Sampler(0));
	list->SetGraphicsRootDescriptorTable(ROOT_SAMPLER1, Get_Sampler(1));
	list->IASetPrimitiveTopology(topology);

	// Direct3D 9 puts pixel centres on integer coordinates; Direct3D 10+ puts them at +0.5.
	// Shifting the viewport by half a pixel reproduces D3D9 rasterisation exactly, which the
	// engine's 2D code (Screen UV Bias) and texel-aligned UI rely on.
	D3D12_VIEWPORT viewport = { float(Viewport.X) + 0.5f, float(Viewport.Y) + 0.5f, float(Viewport.Width), float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ };
	D3D12_RECT scissor = { LONG(Viewport.X), LONG(Viewport.Y), LONG(Viewport.X + Viewport.Width), LONG(Viewport.Y + Viewport.Height) };
	list->RSSetViewports(1, &viewport);
	list->RSSetScissorRects(1, &scissor);

	list->DrawIndexedInstanced(index_count, 1, start_index, base_vertex, 0);
	++Stats.Draws;
	++Stats.PipelineBinds;
	++Stats.ConstantUploads;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	if (Native_Device.Is_Lost()) {
		return D3D_OK;
	}
	Ensure_Frame();
	if (TargetsDirty) {
		Bind_Targets();
	}

	// D3D9 clears the viewport (or the given rectangles inside it).
	std::vector<D3D12_RECT> clear_rects;
	const D3D12_RECT viewport = { LONG(Viewport.X), LONG(Viewport.Y), LONG(Viewport.X + Viewport.Width), LONG(Viewport.Y + Viewport.Height) };
	if (count == 0 || rects == nullptr) {
		clear_rects.push_back(viewport);
	} else {
		for (DWORD i = 0; i < count; ++i) {
			D3D12_RECT r = { std::max(rects[i].x1, viewport.left), std::max(rects[i].y1, viewport.top),
				std::min(rects[i].x2, viewport.right), std::min(rects[i].y2, viewport.bottom) };
			if (r.right > r.left && r.bottom > r.top) {
				clear_rects.push_back(r);
			}
		}
		if (clear_rects.empty()) {
			return D3D_OK;
		}
	}

	ID3D12GraphicsCommandList *list = Native_Device.Command_List();
	if (flags & D3DCLEAR_TARGET) {
		float rgba[4];
		Unpack_Color(rgba, color);
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderTarget ? RenderTarget->Texture()->Prepare_For_Rendering() : Native_Device.Back_Buffer_RTV();
		list->ClearRenderTargetView(rtv, rgba, UINT(clear_rects.size()), clear_rects.data());
	}
	if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) && DepthStencil != nullptr) {
		D3D12_CLEAR_FLAGS clear_flags = D3D12_CLEAR_FLAGS(0);
		if (flags & D3DCLEAR_ZBUFFER) clear_flags |= D3D12_CLEAR_FLAG_DEPTH;
		if (flags & D3DCLEAR_STENCIL) clear_flags |= D3D12_CLEAR_FLAG_STENCIL;
		list->ClearDepthStencilView(Native_Device.Depth_DSV(), clear_flags, z, UINT8(stencil), UINT(clear_rects.size()), clear_rects.data());
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Present(const RECT *, const RECT *, HWND, const RGNDATA *)
{
	if (Native_Device.Is_Lost()) {
		return D3DERR_DEVICELOST;
	}
	Ensure_Frame();
	const bool ok = Native_Device.End_Frame(true, SyncInterval);
	TargetsDirty = true;
	if (StatsEnabled) {
		Record_Frame_Stats();
	}
	return ok ? D3D_OK : D3DERR_DEVICELOST;
}

void D3D12RenderDevice::Record_Frame_Stats()
{
	LARGE_INTEGER now, frequency;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&frequency);
	if (LastPresentTicks != 0) {
		Stats.FrameSeconds += double(now.QuadPart - LastPresentTicks) / double(frequency.QuadPart);
		Stats.WaitSeconds += Native_Device.Take_Wait_Seconds();
		Stats.UploadBytes += Native_Device.Last_Frame_Upload_Bytes();
		Stats.GPUMs += Native_Device.Last_GPU_Frame_Ms();
		++Stats.Frames;
	} else {
		Native_Device.Take_Wait_Seconds();
	}
	LastPresentTicks = now.QuadPart;

	if (Stats.FrameSeconds >= 2.0 && Stats.Frames > 0) {
		const double frames = double(Stats.Frames);
		WWRELEASE_SAY(("D3D12 stats: %.1f fps, frame %.2f ms (cpu %.2f ms, waiting %.2f ms), %.0f draws, %.0f pipeline binds, %.0f constant uploads, %.2f MB uploaded per frame, %u pipelines\n",
			frames / Stats.FrameSeconds, 1000.0 * Stats.FrameSeconds / frames,
			1000.0 * (Stats.FrameSeconds - Stats.WaitSeconds) / frames, 1000.0 * Stats.WaitSeconds / frames,
			double(Stats.Draws) / frames, double(Stats.PipelineBinds) / frames, double(Stats.ConstantUploads) / frames,
			double(Stats.UploadBytes) / frames / (1024.0 * 1024.0), unsigned(Pipelines.size())));
		WWRELEASE_SAY(("D3D12 stats: backend draw path %.3f ms per frame, gpu %.3f ms per frame\n", 1000.0 * double(Stats.DrawTicks) / double(frequency.QuadPart) / frames, Stats.GPUMs / frames));
		Stats = FrameStats();
	}
}

HRESULT D3D12RenderDevice::TestCooperativeLevel()
{
	return Native_Device.Is_Lost() ? D3DERR_DEVICELOST : D3D_OK;
}

HRESULT D3D12RenderDevice::Reset(D3DPRESENT_PARAMETERS *params)
{
	if (params == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (Native_Device.Is_In_Frame()) {
		Native_Device.End_Frame(false, 0);
	}
	if (!Native_Device.Resize(params->BackBufferWidth, params->BackBufferHeight)) {
		return D3DERR_INVALIDCALL;
	}
	switch (params->PresentationInterval) {
	case D3DPRESENT_INTERVAL_IMMEDIATE:	SyncInterval = 0; break;
	case D3DPRESENT_INTERVAL_TWO:			SyncInterval = 2; break;
	default:										SyncInterval = 1; break;
	}
	Reset_State();
	DepthStencil = params->EnableAutoDepthStencil ? DepthSurface : nullptr;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetDeviceCaps(D3DCAPS9 *caps)
{
	if (caps == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	Fill_Caps(*caps, 0);
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetDisplayMode(UINT, D3DDISPLAYMODE *mode)
{
	if (mode == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	mode->Width = Native_Device.Width();
	mode->Height = Native_Device.Height();
	mode->RefreshRate = 60;
	mode->Format = D3DFMT_X8R8G8B8;
	return D3D_OK;
}

UINT D3D12RenderDevice::GetAvailableTextureMem()
{
	ComPtr<IDXGIAdapter3> adapter;
	if (Native_Device.Adapter() && SUCCEEDED(Native_Device.Adapter()->QueryInterface(IID_PPV_ARGS(&adapter)))) {
		DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
		if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) && info.Budget > info.CurrentUsage) {
			return UINT(std::min<UINT64>(info.Budget - info.CurrentUsage, 0xffffffffu));
		}
	}
	return 0x80000000u;
}

void D3D12RenderDevice::SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *)
{
	// Windowed flip-model swap chains have no hardware gamma ramp. A post-process pass
	// will apply it; until then the ramp is ignored.
	static bool warned = false;
	if (!warned) {
		warned = true;
		WWRELEASE_SAY(("D3D12: gamma ramp is not applied yet\n"));
	}
}

HRESULT D3D12RenderDevice::CreateVertexBuffer(UINT length, DWORD usage, DWORD, D3DPOOL pool, W3DVertexBuffer **buffer, HANDLE *)
{
	if (buffer == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	D3D12VertexBuffer *vb = new D3D12VertexBuffer(this, length, usage, pool);
	if (!vb->Buffer.Create_GPU_Resource()) {
		vb->Release();
		*buffer = nullptr;
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*buffer = vb;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DIndexBuffer **buffer, HANDLE *)
{
	if (buffer == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	D3D12IndexBuffer *ib = new D3D12IndexBuffer(this, length, usage, pool, format);
	if (!ib->Buffer.Create_GPU_Resource()) {
		ib->Release();
		*buffer = nullptr;
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*buffer = ib;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, W3DSurface **surface, HANDLE *)
{
	if (surface == nullptr || !D3D12_Format_Layout(format).Supported) {
		return D3DERR_INVALIDCALL;
	}
	*surface = new D3D12Surface(this, width, height, format, pool);
	return D3D_OK;
}

HRESULT D3D12RenderDevice::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, W3DSwapChain **swap_chain)
{
	// Only the editor tools use additional swap chains.
	if (swap_chain) {
		*swap_chain = nullptr;
	}
	return E_NOTIMPL;
}

HRESULT D3D12RenderDevice::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, W3DSurface **surface)
{
	if (surface == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	BackBufferSurface->AddRef();
	*surface = BackBufferSurface;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetRenderTarget(DWORD index, W3DSurface **surface)
{
	if (surface == nullptr || index != 0) {
		return D3DERR_INVALIDCALL;
	}
	D3D12Surface *target = RenderTarget ? RenderTarget : BackBufferSurface;
	target->AddRef();
	*surface = target;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetRenderTarget(DWORD index, W3DSurface *surface)
{
	if (index != 0 || surface == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	D3D12Surface *target = static_cast<D3D12Surface *>(surface);
	D3D12Surface *next = nullptr;
	if (target->Kind() == D3D12Surface::KIND_TEXTURE_LEVEL && target->Texture()->Is_Render_Target()) {
		next = target;
		next->AddRef();
	} else if (target->Kind() != D3D12Surface::KIND_BACK_BUFFER) {
		return D3DERR_INVALIDCALL;
	}
	if (RenderTarget) {
		RenderTarget->Release();
	}
	RenderTarget = next;
	TargetsDirty = true;

	// D3D9 resets the viewport to cover the new render target.
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = RenderTarget ? RenderTarget->Width() : Native_Device.Width();
	Viewport.Height = RenderTarget ? RenderTarget->Height() : Native_Device.Height();
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetDepthStencilSurface(W3DSurface **surface)
{
	if (surface == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (DepthStencil == nullptr) {
		*surface = nullptr;
		return D3DERR_NOTFOUND;
	}
	DepthStencil->AddRef();
	*surface = DepthStencil;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetDepthStencilSurface(W3DSurface *surface)
{
	D3D12Surface *depth = static_cast<D3D12Surface *>(surface);
	if (depth != nullptr && depth->Kind() != D3D12Surface::KIND_DEPTH) {
		return D3DERR_INVALIDCALL;
	}
	DepthStencil = depth;
	TargetsDirty = true;
	return D3D_OK;
}

bool D3D12RenderDevice::Read_Back_Buffer(std::vector<uint32_t> &pixels, UINT &width, UINT &height)
{
	Ensure_Frame();
	unsigned w = 0, h = 0;
	const bool ok = Native_Device.Read_Back_Buffer(pixels, w, h);
	width = w;
	height = h;
	TargetsDirty = true;
	return ok;
}

bool D3D12RenderDevice::Read_Texture(D3D12Texture *texture, std::vector<uint32_t> &pixels)
{
	ID3D12Resource *resource = texture->Resource();
	if (resource == nullptr) {
		return false;
	}
	const D3D12_RESOURCE_DESC desc = resource->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT64 total = 0;
	Native_Device.Device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC buffer = {};
	buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	buffer.Width = total;
	buffer.Height = 1;
	buffer.DepthOrArraySize = 1;
	buffer.MipLevels = 1;
	buffer.SampleDesc.Count = 1;
	buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> readback;
	if (FAILED(Native_Device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
		return false;
	}

	texture->Transition(D3D12_RESOURCE_STATE_COPY_SOURCE);
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = readback.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = resource;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Native_Device.Command_List()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	Native_Device.Flush();
	TargetsDirty = true;

	const UINT width = UINT(desc.Width);
	const UINT height = desc.Height;
	pixels.resize(size_t(width) * height);
	void *mapped = nullptr;
	const D3D12_RANGE range = { 0, SIZE_T(total) };
	if (FAILED(readback->Map(0, &range, &mapped))) {
		return false;
	}
	for (UINT y = 0; y < height; ++y) {
		memcpy(&pixels[size_t(y) * width], static_cast<const uint8_t *>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch, size_t(width) * 4);
	}
	const D3D12_RANGE none = { 0, 0 };
	readback->Unmap(0, &none);
	return true;
}

HRESULT D3D12RenderDevice::GetFrontBufferData(UINT, W3DSurface *dest)
{
	D3D12Surface *target = static_cast<D3D12Surface *>(dest);
	uint8_t *data = target ? target->CPU_Data() : nullptr;
	if (data == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (!Read_Back_Buffer(pixels, width, height)) {
		return D3DERR_INVALIDCALL;
	}
	const UINT w = std::min(width, target->Width());
	const UINT h = std::min(height, target->Height());
	D3D12_Encode(target->Format(), pixels.data(), width, w, h, data, target->CPU_Pitch());
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetRenderTargetData(W3DSurface *render_target, W3DSurface *dest)
{
	D3D12Surface *source = static_cast<D3D12Surface *>(render_target);
	D3D12Surface *target = static_cast<D3D12Surface *>(dest);
	if (source == nullptr || target == nullptr || target->CPU_Data() == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (!source->Read_Pixels(pixels, width, height)) {
		return D3DERR_INVALIDCALL;
	}
	const UINT w = std::min(width, target->Width());
	const UINT h = std::min(height, target->Height());
	if (!D3D12_Encode(target->Format(), pixels.data(), width, w, h, target->CPU_Data(), target->CPU_Pitch())) {
		return D3DERR_INVALIDCALL;
	}
	target->Touch();
	return D3D_OK;
}

HRESULT D3D12RenderDevice::UpdateSurface(W3DSurface *source, const RECT *source_rect, W3DSurface *dest, const POINT *dest_point)
{
	D3D12Surface *src = static_cast<D3D12Surface *>(source);
	D3D12Surface *dst = static_cast<D3D12Surface *>(dest);
	if (src == nullptr || dst == nullptr || src->Format() != dst->Format() || src->CPU_Data() == nullptr || dst->CPU_Data() == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	RECT rect = source_rect ? *source_rect : RECT{ 0, 0, LONG(src->Width()), LONG(src->Height()) };
	const POINT point = dest_point ? *dest_point : POINT{ 0, 0 };
	rect.right = std::min<LONG>(rect.right, rect.left + LONG(dst->Width()) - point.x);
	rect.bottom = std::min<LONG>(rect.bottom, rect.top + LONG(dst->Height()) - point.y);
	if (rect.right <= rect.left || rect.bottom <= rect.top) {
		return D3D_OK;
	}

	const D3D12FormatLayout layout = D3D12_Format_Layout(src->Format());
	const UINT block = layout.Compressed ? 4 : 1;
	const UINT rows = (UINT(rect.bottom - rect.top) + block - 1) / block;
	const UINT row_bytes = ((UINT(rect.right - rect.left) + block - 1) / block) * layout.BytesPerElement;
	const UINT src_pitch = src->CPU_Pitch();
	const UINT dst_pitch = dst->CPU_Pitch();
	const uint8_t *from = src->CPU_Data() + (UINT(rect.top) / block) * src_pitch + (UINT(rect.left) / block) * layout.BytesPerElement;
	uint8_t *to = dst->CPU_Data() + (UINT(point.y) / block) * dst_pitch + (UINT(point.x) / block) * layout.BytesPerElement;
	for (UINT y = 0; y < rows; ++y) {
		memcpy(to + size_t(y) * dst_pitch, from + size_t(y) * src_pitch, row_bytes);
	}
	dst->Touch();
	return D3D_OK;
}

HRESULT D3D12RenderDevice::UpdateTexture(W3DBaseTexture *source, W3DBaseTexture *dest)
{
	D3D12Texture *src = static_cast<D3D12Texture *>(static_cast<W3DTexture *>(source));
	D3D12Texture *dst = static_cast<D3D12Texture *>(static_cast<W3DTexture *>(dest));
	if (src == nullptr || dst == nullptr || src->Format() != dst->Format()) {
		return D3DERR_INVALIDCALL;
	}
	// Match levels by size, as D3D9 does when the source has extra top levels.
	for (UINT d = 0; d < dst->GetLevelCount(); ++d) {
		for (UINT s = 0; s < src->GetLevelCount(); ++s) {
			if (src->Level_Width(s) == dst->Level_Width(d) && src->Level_Height(s) == dst->Level_Height(d)) {
				if (src->Level_Data(s) && dst->Level_Data(d)) {
					memcpy(dst->Level_Data(d), src->Level_Data(s), D3D12_Level_Size(dst->Format(), dst->Level_Width(d), dst->Level_Height(d)));
					dst->Mark_Dirty(d);
				}
				break;
			}
		}
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetViewport(const D3DVIEWPORT9 *viewport)
{
	if (viewport == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	Viewport = *viewport;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
	if (matrix == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (state == D3DTS_WORLD) {
		World = *matrix;
	} else if (state == D3DTS_VIEW) {
		View = *matrix;
	} else if (state == D3DTS_PROJECTION) {
		Projection = *matrix;
	} else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7) {
		TextureMatrices[state - D3DTS_TEXTURE0] = *matrix;
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if (matrix == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (state == D3DTS_WORLD) {
		*matrix = World;
	} else if (state == D3DTS_VIEW) {
		*matrix = View;
	} else if (state == D3DTS_PROJECTION) {
		*matrix = Projection;
	} else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7) {
		*matrix = TextureMatrices[state - D3DTS_TEXTURE0];
	} else {
		*matrix = IDENTITY;
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetMaterial(const D3DMATERIAL9 *material)
{
	if (material == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	Material = *material;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetLight(DWORD index, const D3DLIGHT9 *light)
{
	if (light == nullptr || index >= 8) {
		return D3DERR_INVALIDCALL;
	}
	Lights[index] = *light;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::LightEnable(DWORD index, BOOL enable)
{
	if (index >= 8) {
		return D3DERR_INVALIDCALL;
	}
	LightEnabled[index] = enable != FALSE;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
	if (unsigned(state) < 256) {
		RenderStates[state] = value;
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
	if (stage < 8 && unsigned(state) < 33) {
		StageStates[stage][state] = value;
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value)
{
	if (sampler < 8 && unsigned(state) < 14) {
		SamplerStates[sampler][state] = value;
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetTexture(DWORD stage, W3DBaseTexture *texture)
{
	if (stage >= 8) {
		return D3D_OK;
	}
	D3D12Texture *next = texture ? static_cast<D3D12Texture *>(static_cast<W3DTexture *>(texture)) : nullptr;
	if (next) next->AddRef();
	if (Textures[stage]) Textures[stage]->Release();
	Textures[stage] = next;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetStreamSource(UINT stream, W3DVertexBuffer *buffer, UINT offset, UINT stride)
{
	if (stream != 0) {
		return D3D_OK;
	}
	D3D12VertexBuffer *next = static_cast<D3D12VertexBuffer *>(buffer);
	if (next) next->AddRef();
	if (Stream) Stream->Release();
	Stream = next;
	StreamOffset = offset;
	StreamStride = stride;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::SetIndices(W3DIndexBuffer *buffer)
{
	D3D12IndexBuffer *next = static_cast<D3D12IndexBuffer *>(buffer);
	if (next) next->AddRef();
	if (Indices) Indices->Release();
	Indices = next;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Create_Texture(UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DTexture **texture)
{
	if (texture == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	*texture = nullptr;
	if (!D3D12_Format_Layout(format).Supported) {
		// Like D3DX, fall back to a format that can hold the data.
		format = D3DFMT_A8R8G8B8;
	}
	if ((usage & D3DUSAGE_RENDERTARGET) && !D3D12_Is_Render_Target_Format(format)) {
		return D3DERR_NOTAVAILABLE;
	}
	D3D12Texture *created = new D3D12Texture(this, width, height, mip_levels, usage, format, pool);
	if (!created->Create_GPU_Resource()) {
		created->Release();
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*texture = created;
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Load_Surface_From_Surface(W3DSurface *dest, const RECT *dest_rect, W3DSurface *source, const RECT *source_rect, W3DImageFilter filter)
{
	D3D12Surface *src = static_cast<D3D12Surface *>(source);
	D3D12Surface *dst = static_cast<D3D12Surface *>(dest);
	if (src == nullptr || dst == nullptr || dst->CPU_Data() == nullptr) {
		return D3DERR_INVALIDCALL;
	}

	const RECT s = source_rect ? *source_rect : RECT{ 0, 0, LONG(src->Width()), LONG(src->Height()) };
	const RECT d = dest_rect ? *dest_rect : RECT{ 0, 0, LONG(dst->Width()), LONG(dst->Height()) };
	const UINT sw = UINT(std::max<LONG>(0, s.right - s.left)), sh = UINT(std::max<LONG>(0, s.bottom - s.top));
	const UINT dw = UINT(std::max<LONG>(0, d.right - d.left)), dh = UINT(std::max<LONG>(0, d.bottom - d.top));
	if (sw == 0 || sh == 0 || dw == 0 || dh == 0) {
		return D3D_OK;
	}

	// Same format and size: copy the bits unchanged (keeps compressed and bump data intact).
	if (src->Format() == dst->Format() && sw == dw && sh == dh && src->CPU_Data() != nullptr) {
		const POINT point = { d.left, d.top };
		return UpdateSurface(source, &s, dest, &point);
	}
	if (D3D12_Format_Layout(dst->Format()).Compressed) {
		WWDEBUG_WARNING(("D3D12: cannot convert into a compressed surface\n"));
		return D3DERR_INVALIDCALL;
	}

	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (!src->Read_Pixels(pixels, width, height)) {
		return D3DERR_INVALIDCALL;
	}
	if (UINT(s.right) > width || UINT(s.bottom) > height) {
		return D3DERR_INVALIDCALL;
	}

	std::vector<uint32_t> scaled(size_t(dw) * dh);
	const bool point_filter = (filter == W3D_FILTER_NONE || filter == W3D_FILTER_POINT);
	D3D12_Scale(pixels.data() + size_t(s.top) * width + s.left, width, sw, sh, scaled.data(), dw, dw, dh, point_filter);

	const D3D12FormatLayout layout = D3D12_Format_Layout(dst->Format());
	uint8_t *out = dst->CPU_Data() + size_t(d.top) * dst->CPU_Pitch() + size_t(d.left) * layout.BytesPerElement;
	const UINT cw = std::min(dw, dst->Width() - UINT(d.left));
	const UINT ch = std::min(dh, dst->Height() - UINT(d.top));
	D3D12_Encode(dst->Format(), scaled.data(), dw, cw, ch, out, dst->CPU_Pitch());
	dst->Touch();
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Filter_Texture(W3DBaseTexture *texture, W3DImageFilter filter)
{
	D3D12Texture *tex = static_cast<D3D12Texture *>(static_cast<W3DTexture *>(texture));
	if (tex == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (D3D12_Format_Layout(tex->Format()).Compressed) {
		return D3D_OK;	// compressed textures arrive with their mip chain
	}
	std::vector<uint32_t> upper, lower;
	for (UINT level = 1; level < tex->GetLevelCount(); ++level) {
		const UINT uw = tex->Level_Width(level - 1), uh = tex->Level_Height(level - 1);
		const UINT lw = tex->Level_Width(level), lh = tex->Level_Height(level);
		if (tex->Level_Data(level - 1) == nullptr || tex->Level_Data(level) == nullptr) {
			return D3DERR_INVALIDCALL;
		}
		upper.resize(size_t(uw) * uh);
		lower.resize(size_t(lw) * lh);
		D3D12_Decode(tex->Format(), tex->Level_Data(level - 1), tex->Level_Pitch(level - 1), uw, uh, upper.data(), uw);
		D3D12_Scale(upper.data(), uw, uw, uh, lower.data(), lw, lw, lh, filter == W3D_FILTER_POINT);
		D3D12_Encode(tex->Format(), lower.data(), lw, lw, lh, tex->Level_Data(level), tex->Level_Pitch(level));
		tex->Mark_Dirty(level);
	}
	return D3D_OK;
}

HRESULT D3D12RenderDevice::Save_Surface_To_TGA(const char *filename, W3DSurface *surface)
{
	D3D12Surface *source = static_cast<D3D12Surface *>(surface);
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (filename == nullptr || source == nullptr || !source->Read_Pixels(pixels, width, height)) {
		return D3DERR_INVALIDCALL;
	}
	if (source->Kind() == D3D12Surface::KIND_BACK_BUFFER) {
		for (uint32_t &p : pixels) {
			p |= 0xff000000u;	// the swap chain alpha is undefined
		}
	}
	return Write_TGA(filename, pixels, width, height) ? D3D_OK : E_FAIL;
}

/***********************************************************************************************
** D3D12RenderSystem
***********************************************************************************************/

namespace
{
	class D3D12RenderSystem : public D3D12RefCounted<W3DRenderSystem>
	{
	public:
		bool Init();

		UINT		GetAdapterCount() override { return UINT(Adapters.size()); }
		HRESULT	GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9 *identifier) override;
		UINT		GetAdapterModeCount(UINT adapter, D3DFORMAT format) override;
		HRESULT	EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *display_mode) override;
		HRESULT	GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override;
		HRESULT	GetDeviceCaps(UINT adapter, D3DDEVTYPE, D3DCAPS9 *caps) override;
		HRESULT	CheckDeviceFormat(UINT adapter, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE, D3DFORMAT check_format) override;
		HRESULT	CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT depth_format) override
		{
			return D3D12_Is_Depth_Format(depth_format) ? D3D_OK : D3DERR_NOTAVAILABLE;
		}
		HRESULT	CreateDevice(UINT adapter, D3DDEVTYPE, HWND window, DWORD, D3DPRESENT_PARAMETERS *params, W3DRenderDevice **device) override;
		const char *	Get_Backend_Name() const override { return "Direct3D 12"; }

	private:
		struct AdapterInfo
		{
			ComPtr<IDXGIAdapter1>			Adapter;
			D3DADAPTER_IDENTIFIER9			Identifier = {};
			std::vector<D3DDISPLAYMODE>	Modes;
			D3DDISPLAYMODE						Current = {};
		};

		static bool Is_Display_Format(D3DFORMAT format)
		{
			return format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 || format == D3DFMT_R5G6B5 ||
				format == D3DFMT_X1R5G5B5 || format == D3DFMT_R8G8B8;
		}

		ComPtr<IDXGIFactory4>		Factory;
		std::vector<AdapterInfo>	Adapters;
	};

	bool D3D12RenderSystem::Init()
	{
		if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)))) {
			return false;
		}
		ComPtr<IDXGIFactory6> factory6;
		Factory.As(&factory6);

		for (UINT index = 0; ; ++index) {
			ComPtr<IDXGIAdapter1> adapter;
			const HRESULT hr = factory6
				? factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))
				: Factory->EnumAdapters1(index, &adapter);
			if (hr == DXGI_ERROR_NOT_FOUND) {
				break;
			}
			if (FAILED(hr)) {
				continue;
			}
			DXGI_ADAPTER_DESC1 desc = {};
			adapter->GetDesc1(&desc);
			if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
				FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
				continue;
			}

			AdapterInfo info;
			info.Adapter = adapter;
			D3DADAPTER_IDENTIFIER9 &id = info.Identifier;
			strncpy(id.Description, Narrow(desc.Description).c_str(), sizeof(id.Description) - 1);
			strncpy(id.Driver, "d3d12.dll", sizeof(id.Driver) - 1);
			LARGE_INTEGER version = {};
			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
				id.DriverVersion = version;
			}
			id.VendorId = desc.VendorId;
			id.DeviceId = desc.DeviceId;
			id.SubSysId = desc.SubSysId;
			id.Revision = desc.Revision;
			memcpy(&id.DeviceIdentifier, &desc.AdapterLuid, sizeof(desc.AdapterLuid));
			id.WHQLLevel = 1;

			ComPtr<IDXGIOutput> output;
			if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
				DXGI_OUTPUT_DESC output_desc = {};
				output->GetDesc(&output_desc);
				strncpy(id.DeviceName, Narrow(output_desc.DeviceName).c_str(), sizeof(id.DeviceName) - 1);

				UINT count = 0;
				output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, nullptr);
				std::vector<DXGI_MODE_DESC> modes(count);
				if (count > 0 && SUCCEEDED(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, modes.data()))) {
					for (const DXGI_MODE_DESC &mode : modes) {
						const UINT refresh = mode.RefreshRate.Denominator ? mode.RefreshRate.Numerator / mode.RefreshRate.Denominator : 60;
						auto same = std::find_if(info.Modes.begin(), info.Modes.end(), [&](const D3DDISPLAYMODE &m) {
							return m.Width == mode.Width && m.Height == mode.Height;
						});
						if (same == info.Modes.end()) {
							info.Modes.push_back({ mode.Width, mode.Height, refresh, D3DFMT_X8R8G8B8 });
						} else {
							same->RefreshRate = std::max(same->RefreshRate, refresh);
						}
					}
				}

				DEVMODEW devmode = {};
				devmode.dmSize = sizeof(devmode);
				if (EnumDisplaySettingsW(output_desc.DeviceName, ENUM_CURRENT_SETTINGS, &devmode)) {
					info.Current = { devmode.dmPelsWidth, devmode.dmPelsHeight, devmode.dmDisplayFrequency, D3DFMT_X8R8G8B8 };
				}
			}
			Adapters.push_back(info);
		}

		// GPUs without outputs (laptop discrete GPUs) present through the display adapter.
		const AdapterInfo *display = nullptr;
		for (const AdapterInfo &info : Adapters) {
			if (!info.Modes.empty()) {
				display = &info;
				break;
			}
		}
		for (AdapterInfo &info : Adapters) {
			if (info.Modes.empty() && display != nullptr) {
				info.Modes = display->Modes;
				info.Current = display->Current;
			}
			if (info.Current.Width == 0) {
				info.Current = { UINT(GetSystemMetrics(SM_CXSCREEN)), UINT(GetSystemMetrics(SM_CYSCREEN)), 60, D3DFMT_X8R8G8B8 };
			}
		}
		return !Adapters.empty();
	}

	HRESULT D3D12RenderSystem::GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9 *identifier)
	{
		if (adapter >= Adapters.size() || identifier == nullptr) {
			return D3DERR_INVALIDCALL;
		}
		*identifier = Adapters[adapter].Identifier;
		return D3D_OK;
	}

	UINT D3D12RenderSystem::GetAdapterModeCount(UINT adapter, D3DFORMAT format)
	{
		if (adapter >= Adapters.size() || !Is_Display_Format(format)) {
			return 0;
		}
		return UINT(Adapters[adapter].Modes.size());
	}

	HRESULT D3D12RenderSystem::EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *display_mode)
	{
		if (adapter >= Adapters.size() || display_mode == nullptr || mode >= Adapters[adapter].Modes.size() || !Is_Display_Format(format)) {
			return D3DERR_INVALIDCALL;
		}
		*display_mode = Adapters[adapter].Modes[mode];
		// Every display format renders through the same 32-bit swap chain.
		display_mode->Format = format;
		return D3D_OK;
	}

	HRESULT D3D12RenderSystem::GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode)
	{
		if (adapter >= Adapters.size() || mode == nullptr) {
			return D3DERR_INVALIDCALL;
		}
		*mode = Adapters[adapter].Current;
		return D3D_OK;
	}

	HRESULT D3D12RenderSystem::GetDeviceCaps(UINT adapter, D3DDEVTYPE, D3DCAPS9 *caps)
	{
		if (adapter >= Adapters.size() || caps == nullptr) {
			return D3DERR_INVALIDCALL;
		}
		Fill_Caps(*caps, adapter);
		return D3D_OK;
	}

	HRESULT D3D12RenderSystem::CheckDeviceFormat(UINT adapter, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE, D3DFORMAT check_format)
	{
		if (adapter >= Adapters.size()) {
			return D3DERR_INVALIDCALL;
		}
		if (usage & D3DUSAGE_DEPTHSTENCIL) {
			return D3D12_Is_Depth_Format(check_format) ? D3D_OK : D3DERR_NOTAVAILABLE;
		}
		if (usage & D3DUSAGE_RENDERTARGET) {
			return D3D12_Is_Render_Target_Format(check_format) ? D3D_OK : D3DERR_NOTAVAILABLE;
		}
		return D3D12_Format_Layout(check_format).Supported ? D3D_OK : D3DERR_NOTAVAILABLE;
	}

	HRESULT D3D12RenderSystem::CreateDevice(UINT adapter, D3DDEVTYPE, HWND window, DWORD, D3DPRESENT_PARAMETERS *params, W3DRenderDevice **device)
	{
		if (adapter >= Adapters.size() || params == nullptr || device == nullptr) {
			return D3DERR_INVALIDCALL;
		}
		*device = nullptr;
		D3D12RenderDevice *created = new D3D12RenderDevice();
		if (!created->Init(Adapters[adapter].Adapter.Get(), window, *params)) {
			created->Release();
			return D3DERR_NOTAVAILABLE;
		}
		*device = created;
		return D3D_OK;
	}
}

W3DRenderSystem *W3D_Create_D3D12_Render_System()
{
	D3D12RenderSystem *system = new D3D12RenderSystem();
	if (!system->Init()) {
		WWRELEASE_WARNING(("D3D12: no adapter supports Direct3D 12 at feature level 11_0\n"));
		system->Release();
		return nullptr;
	}
	return system;
}

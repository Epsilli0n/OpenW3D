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

/*
** Native Direct3D 12 renderer backend (private to the backend sources).
**
** Resources keep a CPU copy in the engine's format so Lock() behaves as it always has.
** Static buffers and textures upload changed data when they are unlocked or first used;
** dynamic buffers are copied into the upload ring for each draw that reads them, which
** gives exactly the D3D9 DISCARD / NOOVERWRITE results without tracking ranges.
*/

#pragma once

#ifndef W3DRENDER_D3D12_H
#define W3DRENDER_D3D12_H

#include "w3drender.h"
#include "d3d12device.h"
#include "d3d12formats.h"

#include <cstdint>
#include <cstring>
#include <vector>

class D3D12RenderDevice;
class D3D12Texture;

/*
** Reference counting shared by every backend object.
*/
template <class Interface>
class D3D12RefCounted : public Interface
{
public:
	ULONG AddRef() override { return ULONG(InterlockedIncrement(&RefCount)); }
	ULONG Release() override
	{
		const LONG count = InterlockedDecrement(&RefCount);
		if (count == 0) {
			delete this;
		}
		return ULONG(count);
	}

protected:
	~D3D12RefCounted() override = default;

private:
	volatile LONG RefCount = 1;
};

/*
** Vertex or index buffer.
*/
class D3D12Buffer
{
public:
	D3D12Buffer(D3D12RenderDevice *device, UINT size, DWORD usage, D3DPOOL pool);
	~D3D12Buffer();

	bool			Create_GPU_Resource();
	HRESULT		Lock(UINT offset, UINT size, void **data, DWORD flags);
	HRESULT		Unlock();

	bool			Is_Dynamic() const { return Dynamic; }
	UINT			Size() const { return UINT(Shadow.size()); }
	const uint8_t *	Data() const { return Shadow.data(); }
	D3D12_GPU_VIRTUAL_ADDRESS	GPU_Address() const { return Resource ? Resource->GetGPUVirtualAddress() : 0; }

private:
	void			Upload(UINT offset, UINT size);

	D3D12RenderDevice *			Device;
	std::vector<uint8_t>			Shadow;
	ComPtr<ID3D12Resource>		Resource;
	D3D12_RESOURCE_STATES		State = D3D12_RESOURCE_STATE_COMMON;
	bool								Dynamic;
	UINT								LockOffset = 0;
	UINT								LockSize = 0;
	bool								Locked = false;
};

class D3D12VertexBuffer : public D3D12RefCounted<W3DVertexBuffer>
{
public:
	D3D12VertexBuffer(D3D12RenderDevice *device, UINT size, DWORD usage, D3DPOOL pool) : Buffer(device, size, usage, pool) {}
	HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override { return Buffer.Lock(offset, size, data, flags); }
	HRESULT Unlock() override { return Buffer.Unlock(); }

	D3D12Buffer Buffer;
};

class D3D12IndexBuffer : public D3D12RefCounted<W3DIndexBuffer>
{
public:
	D3D12IndexBuffer(D3D12RenderDevice *device, UINT size, DWORD usage, D3DPOOL pool, D3DFORMAT format)
		: Buffer(device, size, usage, pool), Format(format) {}
	HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override { return Buffer.Lock(offset, size, data, flags); }
	HRESULT Unlock() override { return Buffer.Unlock(); }

	D3D12Buffer	Buffer;
	D3DFORMAT	Format;
};

/*
** Surface: a texture level, a CPU-only image, or the swap chain / depth buffer.
*/
class D3D12Surface : public D3D12RefCounted<W3DSurface>
{
public:
	enum KindType { KIND_TEXTURE_LEVEL, KIND_PLAIN, KIND_BACK_BUFFER, KIND_DEPTH };

	// Texture level (holds a reference to the texture).
	D3D12Surface(D3D12Texture *texture, UINT level);
	// CPU-only image.
	D3D12Surface(D3D12RenderDevice *device, UINT width, UINT height, D3DFORMAT format, D3DPOOL pool);
	// Swap chain back buffer or depth buffer.
	D3D12Surface(D3D12RenderDevice *device, KindType kind);

	HRESULT GetDesc(D3DSURFACE_DESC *desc) override;
	HRESULT LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override;
	HRESULT UnlockRect() override;

	KindType			Kind() const { return SurfaceKind; }
	D3D12Texture *	Texture() const { return OwnerTexture; }
	UINT				Level() const { return TextureLevel; }
	UINT				Width() const;
	UINT				Height() const;
	D3DFORMAT		Format() const;

	// CPU pixels in Format(), or nullptr for GPU-only surfaces.
	uint8_t *		CPU_Data();
	UINT				CPU_Pitch() const;
	// Mark CPU data as modified (uploads texture levels).
	void				Touch();

	// Read the surface as 32-bit pixels (reads back GPU-only surfaces).
	bool				Read_Pixels(std::vector<uint32_t> &pixels, UINT &width, UINT &height);

protected:
	~D3D12Surface() override;

private:
	KindType							SurfaceKind;
	D3D12RenderDevice *			Device = nullptr;
	D3D12Texture *					OwnerTexture = nullptr;
	UINT								TextureLevel = 0;

	// Plain surfaces
	UINT								PlainWidth = 0;
	UINT								PlainHeight = 0;
	D3DFORMAT						PlainFormat = D3DFMT_UNKNOWN;
	D3DPOOL							PlainPool = D3DPOOL_SYSTEMMEM;
	std::vector<uint8_t>			PlainData;

	// Back buffer locks read the GPU contents into this.
	std::vector<uint8_t>			LockData;
};

class D3D12Texture : public D3D12RefCounted<W3DTexture>
{
public:
	// Internal textures owned by the device pass hold_device = false to avoid a reference cycle.
	D3D12Texture(D3D12RenderDevice *device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, bool hold_device = true);

	D3D12RenderDevice *	Owner_Device() const { return Device; }

	bool		Create_GPU_Resource();

	DWORD		GetLevelCount() override { return Levels; }
	DWORD		SetPriority(DWORD priority) override { const DWORD old = Priority; Priority = priority; return old; }
	DWORD		GetPriority() override { return Priority; }
	HRESULT	GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override;
	HRESULT	GetSurfaceLevel(UINT level, W3DSurface **surface) override;
	HRESULT	LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override;
	HRESULT	UnlockRect(UINT level) override;

	UINT		Level_Width(UINT level) const { return Width > (1u << level) ? Width >> level : 1u; }
	UINT		Level_Height(UINT level) const { return Height > (1u << level) ? Height >> level : 1u; }
	D3DFORMAT	Format() const { return TextureFormat; }
	D3DPOOL	Pool() const { return TexturePool; }
	bool		Is_Render_Target() const { return RenderTarget; }

	uint8_t *	Level_Data(UINT level) { return level < Shadow.size() && !Shadow[level].empty() ? Shadow[level].data() : nullptr; }
	UINT		Level_Pitch(UINT level) const { return D3D12_Row_Pitch(TextureFormat, Level_Width(level)); }
	void		Mark_Dirty(UINT level) { DirtyLevels |= (1u << level); }

	// Upload pending changes and move to the shader-readable state. Returns the SRV.
	D3D12_GPU_DESCRIPTOR_HANDLE	Prepare_For_Sampling();
	// Move to the render target state. Returns the RTV.
	D3D12_CPU_DESCRIPTOR_HANDLE	Prepare_For_Rendering();
	ID3D12Resource *					Resource() const { return GPUResource.Get(); }
	void									Transition(D3D12_RESOURCE_STATES state);

	// Surface objects are cached so repeated GetSurfaceLevel calls return the same pointer.
	void		Forget_Surface(UINT level) { if (level < Surfaces.size()) Surfaces[level] = nullptr; }

protected:
	~D3D12Texture() override;

private:
	void		Upload_Level(UINT level);

	D3D12RenderDevice *				Device;
	UINT									Width;
	UINT									Height;
	UINT									Levels;
	D3DFORMAT							TextureFormat;
	D3DPOOL								TexturePool;
	DXGI_FORMAT							GPUFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	bool									RenderTarget;
	DWORD									Priority = 0;
	std::vector<std::vector<uint8_t>>	Shadow;
	uint32_t								DirtyLevels = 0;
	ComPtr<ID3D12Resource>			GPUResource;
	D3D12_RESOURCE_STATES			State = D3D12_RESOURCE_STATE_COMMON;
	unsigned								SRV = UINT32_MAX;
	unsigned								RTV = UINT32_MAX;
	std::vector<D3D12Surface *>	Surfaces;
	bool									HoldsDevice;
};

/*
** Pipeline cache key: everything that has to be baked into a PSO.
*/
struct D3D12PipelineKey
{
	uint32_t FVF;
	uint8_t	SrcBlend, DstBlend, BlendEnable, ColorWrite;
	uint8_t	DepthEnable, DepthWrite, DepthFunc, Cull;
	uint8_t	Fill, HasDepth, Topology, Pad;
	int32_t	DepthBias;
	float		SlopeBias;
	uint32_t	RTFormat;

	bool operator==(const D3D12PipelineKey &other) const { return memcmp(this, &other, sizeof(*this)) == 0; }
};

/*
** The device: D3D9-style state tracking on top of D3D12DeviceClass.
*/
class D3D12RenderDevice : public D3D12RefCounted<W3DRenderDevice>
{
public:
	D3D12RenderDevice();
	bool		Init(IDXGIAdapter1 *adapter, HWND window, const D3DPRESENT_PARAMETERS &params);

	D3D12DeviceClass &	Native() { return Native_Device; }
	// Make sure command recording targets the current frame.
	void		Ensure_Frame();
	// Read the current back buffer as 32-bit pixels.
	bool		Read_Back_Buffer(std::vector<uint32_t> &pixels, UINT &width, UINT &height);
	// Read a render target texture level as 32-bit pixels.
	bool		Read_Texture(D3D12Texture *texture, std::vector<uint32_t> &pixels);

	// Frame
	HRESULT	BeginScene() override { return D3D_OK; }
	HRESULT	EndScene() override { return D3D_OK; }
	HRESULT	Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT	Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty) override;
	HRESULT	TestCooperativeLevel() override;
	HRESULT	Reset(D3DPRESENT_PARAMETERS *params) override;
	HRESULT	ValidateDevice(DWORD *passes) override { if (passes) *passes = 1; return D3D_OK; }

	HRESULT	GetDeviceCaps(D3DCAPS9 *caps) override;
	HRESULT	SetSoftwareVertexProcessing(BOOL) override { return D3D_OK; }
	HRESULT	GetDisplayMode(UINT swap_chain, D3DDISPLAYMODE *mode) override;
	UINT		GetAvailableTextureMem() override;
	HRESULT	EvictManagedResources() override { return D3D_OK; }
	void		SetGammaRamp(UINT swap_chain, DWORD flags, const D3DGAMMARAMP *ramp) override;

	HRESULT	CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, W3DVertexBuffer **buffer, HANDLE *shared) override;
	HRESULT	CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DIndexBuffer **buffer, HANDLE *shared) override;
	HRESULT	CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, W3DSurface **surface, HANDLE *shared) override;
	HRESULT	CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *params, W3DSwapChain **swap_chain) override;

	HRESULT	GetBackBuffer(UINT swap_chain, UINT index, D3DBACKBUFFER_TYPE type, W3DSurface **surface) override;
	HRESULT	GetFrontBufferData(UINT swap_chain, W3DSurface *dest) override;
	HRESULT	GetRenderTarget(DWORD index, W3DSurface **surface) override;
	HRESULT	SetRenderTarget(DWORD index, W3DSurface *surface) override;
	HRESULT	GetDepthStencilSurface(W3DSurface **surface) override;
	HRESULT	SetDepthStencilSurface(W3DSurface *surface) override;
	HRESULT	GetRenderTargetData(W3DSurface *render_target, W3DSurface *dest) override;
	HRESULT	UpdateSurface(W3DSurface *source, const RECT *source_rect, W3DSurface *dest, const POINT *dest_point) override;
	HRESULT	UpdateTexture(W3DBaseTexture *source, W3DBaseTexture *dest) override;

	HRESULT	SetViewport(const D3DVIEWPORT9 *viewport) override;
	HRESULT	SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override;
	HRESULT	GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) override;
	HRESULT	SetMaterial(const D3DMATERIAL9 *material) override;
	HRESULT	SetLight(DWORD index, const D3DLIGHT9 *light) override;
	HRESULT	LightEnable(DWORD index, BOOL enable) override;
	HRESULT	SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
	HRESULT	SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) override;
	HRESULT	SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value) override;
	HRESULT	SetTexture(DWORD stage, W3DBaseTexture *texture) override;
	HRESULT	SetNPatchMode(float) override { return D3D_OK; }

	HRESULT	SetStreamSource(UINT stream, W3DVertexBuffer *buffer, UINT offset, UINT stride) override;
	HRESULT	SetIndices(W3DIndexBuffer *buffer) override;
	HRESULT	SetFVF(DWORD fvf) override { FVF = fvf; return D3D_OK; }
	HRESULT	DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count) override;

	HRESULT	Create_Texture(UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DTexture **texture) override;
	HRESULT	Load_Surface_From_Surface(W3DSurface *dest, const RECT *dest_rect, W3DSurface *source, const RECT *source_rect, W3DImageFilter filter) override;
	HRESULT	Filter_Texture(W3DBaseTexture *texture, W3DImageFilter filter) override;
	HRESULT	Save_Surface_To_TGA(const char *filename, W3DSurface *surface) override;

protected:
	~D3D12RenderDevice() override;

private:
	struct Pipeline
	{
		D3D12PipelineKey				Key;
		ComPtr<ID3D12PipelineState>	State;
	};

	void		Reset_State();
	bool		Create_Root_Signature();
	bool		Create_Defaults();
	ID3D12PipelineState *	Get_Pipeline(const D3D12PipelineKey &key);
	D3D12_GPU_DESCRIPTOR_HANDLE	Get_Sampler(unsigned stage);
	void		Bind_Targets();
	void		Fill_Constants(void *data);
	bool		Bind_Geometry(INT &base_vertex, UINT min_index, UINT vertex_count, UINT &start_index, UINT index_count);

	D3D12DeviceClass					Native_Device;
	ComPtr<ID3D12RootSignature>	RootSignature;
	std::vector<Pipeline>			Pipelines;
	struct SamplerEntry { uint64_t Key; unsigned Index; };
	std::vector<SamplerEntry>		Samplers;
	ComPtr<ID3D12Resource>			DefaultVertexData;			// constant inputs for missing vertex elements
	D3D12Texture *						WhiteTexture = nullptr;
	D3D12Surface *						BackBufferSurface = nullptr;
	D3D12Surface *						DepthSurface = nullptr;
	unsigned								SyncInterval = 1;
	uint64_t								BoundEpoch = UINT64_MAX;
	bool									TargetsDirty = true;

	// Optional statistics (W3D_RENDER_STATS=1), logged every two seconds.
	struct FrameStats
	{
		unsigned		Frames = 0;
		uint64_t		Draws = 0;
		uint64_t		PipelineBinds = 0;
		uint64_t		ConstantUploads = 0;
		uint64_t		UploadBytes = 0;
		double		FrameSeconds = 0.0;
		double		WaitSeconds = 0.0;
		int64_t		DrawTicks = 0;
		double		GPUMs = 0.0;
	};
	void		Record_Frame_Stats();
	HRESULT	Draw_Indexed(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count);
	bool									StatsEnabled = false;
	FrameStats							Stats;
	int64_t								LastPresentTicks = 0;

	// D3D9 state
	DWORD									RenderStates[256];
	DWORD									StageStates[8][33];
	DWORD									SamplerStates[8][14];
	D3DMATRIX							World;
	D3DMATRIX							View;
	D3DMATRIX							Projection;
	D3DMATRIX							TextureMatrices[8];
	D3DMATERIAL9						Material;
	D3DLIGHT9							Lights[8];
	bool									LightEnabled[8];
	D3DVIEWPORT9						Viewport;
	D3D12Texture *						Textures[8];
	D3D12VertexBuffer *				Stream = nullptr;
	UINT									StreamOffset = 0;
	UINT									StreamStride = 0;
	D3D12IndexBuffer *				Indices = nullptr;
	DWORD									FVF = 0;
	D3D12Surface *						RenderTarget = nullptr;	// null: back buffer
	D3D12Surface *						DepthStencil = nullptr;	// null: no depth
};

W3DRenderSystem *	W3D_Create_D3D12_Render_System();

#endif

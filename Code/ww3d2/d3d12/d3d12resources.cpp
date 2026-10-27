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

// Buffers, textures and surfaces for the D3D12 backend.

#include "w3drender_d3d12.h"

#include "wwdebug.h"

#include <algorithm>

namespace
{
	D3D12_HEAP_PROPERTIES Default_Heap()
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CreationNodeMask = 1;
		props.VisibleNodeMask = 1;
		return props;
	}

	// Offset of (x, y) inside a level, honouring 4x4 blocks for compressed formats.
	UINT Rect_Offset(D3DFORMAT format, UINT pitch, const RECT *rect)
	{
		if (rect == nullptr) {
			return 0;
		}
		const D3D12FormatLayout layout = D3D12_Format_Layout(format);
		if (layout.Compressed) {
			return UINT(rect->top / 4) * pitch + UINT(rect->left / 4) * layout.BytesPerElement;
		}
		return UINT(rect->top) * pitch + UINT(rect->left) * layout.BytesPerElement;
	}

	unsigned Full_Mip_Count(UINT width, UINT height)
	{
		unsigned count = 1;
		UINT size = std::max(width, height);
		while (size > 1) {
			size >>= 1;
			++count;
		}
		return count;
	}
}

/***********************************************************************************************
** D3D12Buffer
***********************************************************************************************/

D3D12Buffer::D3D12Buffer(D3D12RenderDevice *device, UINT size, DWORD usage, D3DPOOL pool)
	: Device(device),
	  Shadow(size),
	  Dynamic((usage & D3DUSAGE_DYNAMIC) != 0 || pool == D3DPOOL_SYSTEMMEM)
{
	Device->AddRef();
}

D3D12Buffer::~D3D12Buffer()
{
	Device->Native().Defer_Release(std::move(Resource));
	Device->Release();
}

bool D3D12Buffer::Create_GPU_Resource()
{
	if (Dynamic || Shadow.empty()) {
		return true;
	}
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = Shadow.size();
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	const D3D12_HEAP_PROPERTIES heap = Default_Heap();
	const HRESULT hr = Device->Native().Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&Resource));
	if (FAILED(hr)) {
		WWRELEASE_WARNING(("D3D12: buffer creation failed (%u bytes): %s\n", unsigned(Shadow.size()), D3D12_Result_Name(hr)));
		return false;
	}
	State = D3D12_RESOURCE_STATE_COMMON;
	return true;
}

HRESULT D3D12Buffer::Lock(UINT offset, UINT size, void **data, DWORD /*flags*/)
{
	if (data == nullptr || offset > Shadow.size()) {
		return D3DERR_INVALIDCALL;
	}
	if (size == 0 || offset + size > Shadow.size()) {
		size = UINT(Shadow.size()) - offset;
	}
	*data = Shadow.data() + offset;
	LockOffset = offset;
	LockSize = size;
	Locked = true;
	return D3D_OK;
}

HRESULT D3D12Buffer::Unlock()
{
	if (!Locked) {
		return D3DERR_INVALIDCALL;
	}
	Locked = false;
	if (Resource && LockSize > 0) {
		Upload(LockOffset, LockSize);
	}
	return D3D_OK;
}

void D3D12Buffer::Upload(UINT offset, UINT size)
{
	D3D12DeviceClass &native = Device->Native();
	D3D12UploadRingClass::Allocation staging;
	if (!native.Upload().Allocate(size, 16, staging)) {
		return;
	}
	memcpy(staging.CPU, Shadow.data() + offset, size);

	// The copy lands between the draws recorded before and after it, which matches
	// D3D9 locking without DISCARD or NOOVERWRITE.
	const D3D12_RESOURCE_STATES read_state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
	native.Transition(Resource.Get(), State, D3D12_RESOURCE_STATE_COPY_DEST);
	native.Command_List()->CopyBufferRegion(Resource.Get(), offset, staging.Resource, staging.Offset, size);
	native.Transition(Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, read_state);
	State = read_state;
}

/***********************************************************************************************
** D3D12Texture
***********************************************************************************************/

D3D12Texture::D3D12Texture(D3D12RenderDevice *device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, bool hold_device)
	: Device(device),
	  Width(std::max(1u, width)),
	  Height(std::max(1u, height)),
	  TextureFormat(format),
	  TexturePool(pool),
	  RenderTarget((usage & D3DUSAGE_RENDERTARGET) != 0),
	  HoldsDevice(hold_device)
{
	const unsigned full = Full_Mip_Count(Width, Height);
	Levels = (levels == 0 || levels > full) ? full : levels;
	if (RenderTarget) {
		Levels = 1;
	} else {
		Shadow.resize(Levels);
		for (UINT level = 0; level < Levels; ++level) {
			Shadow[level].resize(D3D12_Level_Size(format, Level_Width(level), Level_Height(level)));
		}
		DirtyLevels = (Levels >= 32) ? 0xffffffffu : ((1u << Levels) - 1);
	}
	Surfaces.assign(Levels, nullptr);
	if (HoldsDevice) {
		Device->AddRef();
	}
}

D3D12Texture::~D3D12Texture()
{
	if (Device) {
		D3D12DeviceClass &native = Device->Native();
		native.Defer_Release(std::move(GPUResource));
		native.Defer_Free_SRV(SRV);
		if (RTV != UINT32_MAX) {
			native.RTV_Heap().Free(RTV);
		}
		if (HoldsDevice) {
			Device->Release();
		}
	}
}

bool D3D12Texture::Create_GPU_Resource()
{
	if (TexturePool == D3DPOOL_SYSTEMMEM || TexturePool == D3DPOOL_SCRATCH) {
		return true;
	}

	D3D12DeviceClass &native = Device->Native();
	GPUFormat = RenderTarget ? DXGI_FORMAT_B8G8R8A8_UNORM : D3D12_Upload_Format(TextureFormat, Width, Height);

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = Width;
	desc.Height = Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = UINT16(Levels);
	desc.Format = GPUFormat;
	desc.SampleDesc.Count = 1;
	desc.Flags = RenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format = GPUFormat;
	State = RenderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;

	const D3D12_HEAP_PROPERTIES heap = Default_Heap();
	const HRESULT hr = native.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, State,
		RenderTarget ? &clear : nullptr, IID_PPV_ARGS(&GPUResource));
	if (FAILED(hr)) {
		WWRELEASE_WARNING(("D3D12: texture creation failed (%ux%u, format %d): %s\n", Width, Height, int(TextureFormat), D3D12_Result_Name(hr)));
		return false;
	}

	SRV = native.SRV_Heap().Allocate();
	if (SRV == UINT32_MAX) {
		return false;
	}
	native.Device()->CreateShaderResourceView(GPUResource.Get(), nullptr, native.SRV_Heap().CPU(SRV));

	if (RenderTarget) {
		RTV = native.RTV_Heap().Allocate();
		if (RTV == UINT32_MAX) {
			return false;
		}
		native.Device()->CreateRenderTargetView(GPUResource.Get(), nullptr, native.RTV_Heap().CPU(RTV));
	}
	return true;
}

HRESULT D3D12Texture::GetLevelDesc(UINT level, D3DSURFACE_DESC *desc)
{
	if (desc == nullptr || level >= Levels) {
		return D3DERR_INVALIDCALL;
	}
	memset(desc, 0, sizeof(*desc));
	desc->Format = TextureFormat;
	desc->Type = D3DRTYPE_SURFACE;
	desc->Usage = RenderTarget ? D3DUSAGE_RENDERTARGET : 0;
	desc->Pool = TexturePool;
	desc->MultiSampleType = D3DMULTISAMPLE_NONE;
	desc->Width = Level_Width(level);
	desc->Height = Level_Height(level);
	return D3D_OK;
}

HRESULT D3D12Texture::GetSurfaceLevel(UINT level, W3DSurface **surface)
{
	if (surface == nullptr || level >= Levels) {
		return D3DERR_INVALIDCALL;
	}
	if (Surfaces[level] != nullptr) {
		Surfaces[level]->AddRef();
	} else {
		Surfaces[level] = new D3D12Surface(this, level);
	}
	*surface = Surfaces[level];
	return D3D_OK;
}

HRESULT D3D12Texture::LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD /*flags*/)
{
	if (locked == nullptr || level >= Levels || Shadow.empty()) {
		return D3DERR_INVALIDCALL;
	}
	const UINT pitch = Level_Pitch(level);
	locked->Pitch = INT(pitch);
	locked->pBits = Shadow[level].data() + Rect_Offset(TextureFormat, pitch, rect);
	return D3D_OK;
}

HRESULT D3D12Texture::UnlockRect(UINT level)
{
	if (level >= Levels) {
		return D3DERR_INVALIDCALL;
	}
	Mark_Dirty(level);
	return D3D_OK;
}

void D3D12Texture::Transition(D3D12_RESOURCE_STATES state)
{
	if (GPUResource && State != state) {
		Device->Native().Transition(GPUResource.Get(), State, state);
		State = state;
	}
}

void D3D12Texture::Upload_Level(UINT level)
{
	D3D12DeviceClass &native = Device->Native();
	const D3D12_RESOURCE_DESC desc = GPUResource->GetDesc();

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rows = 0;
	UINT64 row_bytes = 0;
	UINT64 total = 0;
	native.Device()->GetCopyableFootprints(&desc, level, 1, 0, &footprint, &rows, &row_bytes, &total);

	D3D12UploadRingClass::Allocation staging;
	if (!native.Upload().Allocate(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, staging)) {
		return;
	}

	D3D12_Convert_For_Upload(TextureFormat, GPUFormat, Shadow[level].data(), Level_Pitch(level),
		Level_Width(level), Level_Height(level), staging.CPU, footprint.Footprint.RowPitch);

	footprint.Offset = staging.Offset;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = GPUResource.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = level;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = staging.Resource;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;
	native.Command_List()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Texture::Prepare_For_Sampling()
{
	D3D12DeviceClass &native = Device->Native();
	if (!GPUResource && !Create_GPU_Resource()) {
		return {};
	}
	if (DirtyLevels != 0 && !Shadow.empty()) {
		Transition(D3D12_RESOURCE_STATE_COPY_DEST);
		for (UINT level = 0; level < Levels; ++level) {
			if (DirtyLevels & (1u << level)) {
				Upload_Level(level);
			}
		}
		DirtyLevels = 0;
	}
	Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return native.SRV_Heap().GPU(SRV);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Texture::Prepare_For_Rendering()
{
	Transition(D3D12_RESOURCE_STATE_RENDER_TARGET);
	return Device->Native().RTV_Heap().CPU(RTV);
}

/***********************************************************************************************
** D3D12Surface
***********************************************************************************************/

D3D12Surface::D3D12Surface(D3D12Texture *texture, UINT level)
	: SurfaceKind(KIND_TEXTURE_LEVEL), OwnerTexture(texture), TextureLevel(level)
{
	OwnerTexture->AddRef();
}

D3D12Surface::D3D12Surface(D3D12RenderDevice *device, UINT width, UINT height, D3DFORMAT format, D3DPOOL pool)
	: SurfaceKind(KIND_PLAIN),
	  Device(device),
	  PlainWidth(std::max(1u, width)),
	  PlainHeight(std::max(1u, height)),
	  PlainFormat(format),
	  PlainPool(pool)
{
	PlainData.resize(D3D12_Level_Size(format, PlainWidth, PlainHeight));
	Device->AddRef();
}

D3D12Surface::D3D12Surface(D3D12RenderDevice *device, KindType kind)
	: SurfaceKind(kind), Device(device)
{
	// The device owns these and outlives them, so they do not hold a reference to it.
}

D3D12Surface::~D3D12Surface()
{
	if (OwnerTexture != nullptr) {
		OwnerTexture->Forget_Surface(TextureLevel);
		OwnerTexture->Release();
	}
	if (SurfaceKind == KIND_PLAIN) {
		Device->Release();
	}
}

UINT D3D12Surface::Width() const
{
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:	return OwnerTexture->Level_Width(TextureLevel);
	case KIND_PLAIN:				return PlainWidth;
	default:							return Device->Native().Width();
	}
}

UINT D3D12Surface::Height() const
{
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:	return OwnerTexture->Level_Height(TextureLevel);
	case KIND_PLAIN:				return PlainHeight;
	default:							return Device->Native().Height();
	}
}

D3DFORMAT D3D12Surface::Format() const
{
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:	return OwnerTexture->Format();
	case KIND_PLAIN:				return PlainFormat;
	case KIND_BACK_BUFFER:		return D3DFMT_X8R8G8B8;
	default:							return D3DFMT_D24S8;
	}
}

HRESULT D3D12Surface::GetDesc(D3DSURFACE_DESC *desc)
{
	if (desc == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	if (SurfaceKind == KIND_TEXTURE_LEVEL) {
		return OwnerTexture->GetLevelDesc(TextureLevel, desc);
	}
	memset(desc, 0, sizeof(*desc));
	desc->Format = Format();
	desc->Type = D3DRTYPE_SURFACE;
	desc->Width = Width();
	desc->Height = Height();
	desc->MultiSampleType = D3DMULTISAMPLE_NONE;
	switch (SurfaceKind) {
	case KIND_PLAIN:			desc->Pool = PlainPool; break;
	case KIND_BACK_BUFFER:	desc->Pool = D3DPOOL_DEFAULT; desc->Usage = D3DUSAGE_RENDERTARGET; break;
	default:						desc->Pool = D3DPOOL_DEFAULT; desc->Usage = D3DUSAGE_DEPTHSTENCIL; break;
	}
	return D3D_OK;
}

uint8_t *D3D12Surface::CPU_Data()
{
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:	return OwnerTexture->Level_Data(TextureLevel);
	case KIND_PLAIN:				return PlainData.data();
	default:							return nullptr;
	}
}

UINT D3D12Surface::CPU_Pitch() const
{
	return D3D12_Row_Pitch(Format(), Width());
}

void D3D12Surface::Touch()
{
	if (SurfaceKind == KIND_TEXTURE_LEVEL) {
		OwnerTexture->Mark_Dirty(TextureLevel);
	}
}

HRESULT D3D12Surface::LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
	if (locked == nullptr) {
		return D3DERR_INVALIDCALL;
	}
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:
		return OwnerTexture->LockRect(TextureLevel, locked, rect, flags);
	case KIND_PLAIN: {
		const UINT pitch = CPU_Pitch();
		locked->Pitch = INT(pitch);
		locked->pBits = PlainData.data() + Rect_Offset(PlainFormat, pitch, rect);
		return D3D_OK;
	}
	case KIND_BACK_BUFFER: {
		std::vector<uint32_t> pixels;
		UINT width = 0, height = 0;
		if (!Device->Read_Back_Buffer(pixels, width, height)) {
			return D3DERR_INVALIDCALL;
		}
		LockData.resize(pixels.size() * 4);
		memcpy(LockData.data(), pixels.data(), LockData.size());
		const UINT pitch = width * 4;
		locked->Pitch = INT(pitch);
		locked->pBits = LockData.data() + (rect ? UINT(rect->top) * pitch + UINT(rect->left) * 4 : 0);
		return D3D_OK;
	}
	default:
		return D3DERR_INVALIDCALL;
	}
}

HRESULT D3D12Surface::UnlockRect()
{
	switch (SurfaceKind) {
	case KIND_TEXTURE_LEVEL:
		return OwnerTexture->UnlockRect(TextureLevel);
	case KIND_BACK_BUFFER:
		LockData.clear();
		LockData.shrink_to_fit();
		return D3D_OK;
	default:
		return D3D_OK;
	}
}

bool D3D12Surface::Read_Pixels(std::vector<uint32_t> &pixels, UINT &width, UINT &height)
{
	width = Width();
	height = Height();
	if (SurfaceKind == KIND_BACK_BUFFER) {
		return Device->Read_Back_Buffer(pixels, width, height);
	}
	if (SurfaceKind == KIND_TEXTURE_LEVEL && OwnerTexture->Is_Render_Target()) {
		return OwnerTexture->Owner_Device()->Read_Texture(OwnerTexture, pixels);
	}
	const uint8_t *data = CPU_Data();
	if (data == nullptr) {
		return false;
	}
	pixels.resize(size_t(width) * height);
	D3D12_Decode(Format(), data, CPU_Pitch(), width, height, pixels.data(), width);
	return true;
}

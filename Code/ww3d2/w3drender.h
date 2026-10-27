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
** Renderer backend interface.
**
** WW3D talks to the GPU through these interfaces instead of Direct3D 9 directly, so the
** same engine code can run on the Direct3D 9 backend or the native Direct3D 12 backend.
**
** The interfaces deliberately use the method names, parameters and state vocabulary
** (D3DRENDERSTATETYPE, D3DFORMAT, D3DLIGHT9, ...) the engine already speaks. The D3D9
** types come from the headers only; nothing here requires the Direct3D 9 runtime. Each
** backend implements the semantics natively.
**
** Objects are reference counted like COM objects: Release() when done with a pointer
** returned through an out parameter.
*/

#pragma once

#ifndef W3DRENDER_H
#define W3DRENDER_H

#include <d3d9.h>

class W3DSurface;
class W3DBaseTexture;
class W3DTexture;
class W3DVertexBuffer;
class W3DIndexBuffer;
class W3DSwapChain;
class W3DRenderDevice;

class W3DRenderObject
{
public:
	virtual ULONG		AddRef() = 0;
	virtual ULONG		Release() = 0;

protected:
	virtual ~W3DRenderObject() = default;
};

class W3DSurface : public W3DRenderObject
{
public:
	virtual HRESULT	GetDesc(D3DSURFACE_DESC *desc) = 0;
	virtual HRESULT	LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) = 0;
	virtual HRESULT	UnlockRect() = 0;
};

class W3DBaseTexture : public W3DRenderObject
{
public:
	virtual DWORD		GetLevelCount() = 0;
	virtual DWORD		SetPriority(DWORD priority) = 0;
	virtual DWORD		GetPriority() = 0;
};

class W3DTexture : public W3DBaseTexture
{
public:
	virtual HRESULT	GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) = 0;
	virtual HRESULT	GetSurfaceLevel(UINT level, W3DSurface **surface) = 0;
	virtual HRESULT	LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) = 0;
	virtual HRESULT	UnlockRect(UINT level) = 0;
};

class W3DVertexBuffer : public W3DRenderObject
{
public:
	virtual HRESULT	Lock(UINT offset, UINT size, void **data, DWORD flags) = 0;
	virtual HRESULT	Unlock() = 0;
};

class W3DIndexBuffer : public W3DRenderObject
{
public:
	virtual HRESULT	Lock(UINT offset, UINT size, void **data, DWORD flags) = 0;
	virtual HRESULT	Unlock() = 0;
};

class W3DSwapChain : public W3DRenderObject
{
public:
	virtual HRESULT	GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, W3DSurface **surface) = 0;
	virtual HRESULT	Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty, DWORD flags) = 0;
};

// Filters for the image helpers below (same values as D3DX_FILTER_*).
enum W3DImageFilter : DWORD
{
	W3D_FILTER_NONE		= 1,
	W3D_FILTER_POINT		= 2,
	W3D_FILTER_LINEAR		= 3,
	W3D_FILTER_TRIANGLE	= 4,
	W3D_FILTER_BOX			= 5,
};

class W3DRenderDevice : public W3DRenderObject
{
public:
	// Frame
	virtual HRESULT	BeginScene() = 0;
	virtual HRESULT	EndScene() = 0;
	virtual HRESULT	Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) = 0;
	virtual HRESULT	Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty) = 0;
	virtual HRESULT	TestCooperativeLevel() = 0;
	virtual HRESULT	Reset(D3DPRESENT_PARAMETERS *params) = 0;
	virtual HRESULT	ValidateDevice(DWORD *passes) = 0;

	// Capabilities and device-wide settings
	virtual HRESULT	GetDeviceCaps(D3DCAPS9 *caps) = 0;
	virtual HRESULT	SetSoftwareVertexProcessing(BOOL software) = 0;
	virtual HRESULT	GetDisplayMode(UINT swap_chain, D3DDISPLAYMODE *mode) = 0;
	virtual UINT		GetAvailableTextureMem() = 0;
	virtual HRESULT	EvictManagedResources() = 0;
	virtual void		SetGammaRamp(UINT swap_chain, DWORD flags, const D3DGAMMARAMP *ramp) = 0;

	// Resource creation
	virtual HRESULT	CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, W3DVertexBuffer **buffer, HANDLE *shared) = 0;
	virtual HRESULT	CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DIndexBuffer **buffer, HANDLE *shared) = 0;
	virtual HRESULT	CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, W3DSurface **surface, HANDLE *shared) = 0;
	virtual HRESULT	CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *params, W3DSwapChain **swap_chain) = 0;

	// Render targets and copies
	virtual HRESULT	GetBackBuffer(UINT swap_chain, UINT index, D3DBACKBUFFER_TYPE type, W3DSurface **surface) = 0;
	virtual HRESULT	GetFrontBufferData(UINT swap_chain, W3DSurface *dest) = 0;
	virtual HRESULT	GetRenderTarget(DWORD index, W3DSurface **surface) = 0;
	virtual HRESULT	SetRenderTarget(DWORD index, W3DSurface *surface) = 0;
	virtual HRESULT	GetDepthStencilSurface(W3DSurface **surface) = 0;
	virtual HRESULT	SetDepthStencilSurface(W3DSurface *surface) = 0;
	virtual HRESULT	GetRenderTargetData(W3DSurface *render_target, W3DSurface *dest) = 0;
	virtual HRESULT	UpdateSurface(W3DSurface *source, const RECT *source_rect, W3DSurface *dest, const POINT *dest_point) = 0;
	virtual HRESULT	UpdateTexture(W3DBaseTexture *source, W3DBaseTexture *dest) = 0;

	// Fixed-function state
	virtual HRESULT	SetViewport(const D3DVIEWPORT9 *viewport) = 0;
	virtual HRESULT	SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) = 0;
	virtual HRESULT	GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) = 0;
	virtual HRESULT	SetMaterial(const D3DMATERIAL9 *material) = 0;
	virtual HRESULT	SetLight(DWORD index, const D3DLIGHT9 *light) = 0;
	virtual HRESULT	LightEnable(DWORD index, BOOL enable) = 0;
	virtual HRESULT	SetRenderState(D3DRENDERSTATETYPE state, DWORD value) = 0;
	virtual HRESULT	SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) = 0;
	virtual HRESULT	SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value) = 0;
	virtual HRESULT	SetTexture(DWORD stage, W3DBaseTexture *texture) = 0;
	virtual HRESULT	SetNPatchMode(float segments) = 0;

	// Geometry
	virtual HRESULT	SetStreamSource(UINT stream, W3DVertexBuffer *buffer, UINT offset, UINT stride) = 0;
	virtual HRESULT	SetIndices(W3DIndexBuffer *buffer) = 0;
	virtual HRESULT	SetFVF(DWORD fvf) = 0;
	virtual HRESULT	DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count) = 0;

	// Image helpers (formerly D3DX)
	//
	// Create_Texture adjusts the size, mip count and format to what the device supports.
	virtual HRESULT	Create_Texture(UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DTexture **texture) = 0;
	// Copy with scaling and format conversion. Null rects mean the whole surface.
	virtual HRESULT	Load_Surface_From_Surface(W3DSurface *dest, const RECT *dest_rect, W3DSurface *source, const RECT *source_rect, W3DImageFilter filter) = 0;
	// Regenerate mip levels 1..n from level 0.
	virtual HRESULT	Filter_Texture(W3DBaseTexture *texture, W3DImageFilter filter) = 0;
	// Write a surface to a TGA file.
	virtual HRESULT	Save_Surface_To_TGA(const char *filename, W3DSurface *surface) = 0;
};

class W3DRenderSystem : public W3DRenderObject
{
public:
	virtual UINT		GetAdapterCount() = 0;
	virtual HRESULT	GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) = 0;
	virtual UINT		GetAdapterModeCount(UINT adapter, D3DFORMAT format) = 0;
	virtual HRESULT	EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *display_mode) = 0;
	virtual HRESULT	GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) = 0;
	virtual HRESULT	GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) = 0;
	virtual HRESULT	CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT check_format) = 0;
	virtual HRESULT	CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, D3DFORMAT render_target_format, D3DFORMAT depth_format) = 0;
	virtual HRESULT	CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *params, W3DRenderDevice **device) = 0;

	// Short name of the backend, e.g. "Direct3D 9".
	virtual const char *	Get_Backend_Name() const = 0;
};

enum class W3DRendererType
{
	D3D9,
	D3D12,
};

// Create the render system for a backend. Returns nullptr if it is unavailable.
W3DRenderSystem *	W3D_Create_Render_System(W3DRendererType type);

// Human-readable name of an HRESULT returned by a backend.
const char *		W3D_Render_Error_String(HRESULT result);

#endif

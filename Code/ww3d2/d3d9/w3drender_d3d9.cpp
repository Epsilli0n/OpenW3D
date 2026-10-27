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
** Direct3D 9 renderer backend.
**
** Each wrapper owns one reference to the D3D9 object it wraps. Resources store a pointer
** to their wrapper in D3D9 private data, so asking D3D9 for the same surface twice (for
** example GetRenderTarget) yields the same wrapper and pointer comparisons keep working.
*/

#include "w3drender_d3d9.h"

#include "soutil.h"
#include "wwdebug.h"

#include <d3dx9.h>
#include "dxerr_compat.h"

#include <cstdio>

namespace
{
	// {6F3C9A51-2B7E-4D1A-9E33-0D4C1B8A7E21}
	const GUID WRAPPER_GUID = { 0x6f3c9a51, 0x2b7e, 0x4d1a, { 0x9e, 0x33, 0x0d, 0x4c, 0x1b, 0x8a, 0x7e, 0x21 } };

	const char *D3D9_Object_Name()
	{
#ifdef _WIN32
		return "d3d9.dll";
#elif defined (__ELF__)
		return "libdxvk_d3d9.so.0";
#elif defined (__MACH__)
		return "libdxvk_d3d9.0.dylib";
#else
		static_assert(false, "Unknown d3d9 name");
#endif
	}

	/*
	** Reference counted wrapper base. 'Com' is the wrapped D3D9 interface.
	*/
	template <class Interface, class Com>
	class D3D9Object : public Interface
	{
	public:
		explicit D3D9Object(Com *object) : Object(object) {}

		ULONG AddRef() override { return ULONG(InterlockedIncrement(&RefCount)); }
		ULONG Release() override
		{
			const LONG count = InterlockedDecrement(&RefCount);
			if (count == 0) {
				delete this;
			}
			return ULONG(count);
		}

		Com *	Peek() const { return Object; }

	protected:
		~D3D9Object() override
		{
			if (Object != nullptr) {
				Object->Release();
			}
		}

		Com *				Object;
		volatile LONG	RefCount = 1;
	};

	/*
	** Resources that can carry private data map back to a single wrapper.
	*/
	template <class Interface, class Com>
	class D3D9Resource : public D3D9Object<Interface, Com>
	{
	public:
		explicit D3D9Resource(Com *object) : D3D9Object<Interface, Com>(object)
		{
			void *self = this;
			object->SetPrivateData(WRAPPER_GUID, &self, sizeof(self), 0);
		}

	protected:
		~D3D9Resource() override
		{
			if (this->Object != nullptr) {
				this->Object->FreePrivateData(WRAPPER_GUID);
			}
		}
	};

	/*
	** Turn a D3D9 object returned with a reference into a wrapper holding that reference.
	*/
	template <class Wrapper, class Com>
	Wrapper *Wrap(Com *object)
	{
		if (object == nullptr) {
			return nullptr;
		}
		void *existing = nullptr;
		DWORD size = sizeof(existing);
		if (SUCCEEDED(object->GetPrivateData(WRAPPER_GUID, &existing, &size)) && existing != nullptr) {
			Wrapper *wrapper = static_cast<Wrapper *>(existing);
			wrapper->AddRef();
			object->Release();	// the wrapper already owns a reference
			return wrapper;
		}
		return new Wrapper(object);
	}

	class D3D9Surface : public D3D9Resource<W3DSurface, IDirect3DSurface9>
	{
	public:
		using D3D9Resource::D3D9Resource;

		HRESULT GetDesc(D3DSURFACE_DESC *desc) override { return Object->GetDesc(desc); }
		HRESULT LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override { return Object->LockRect(locked, rect, flags); }
		HRESULT UnlockRect() override { return Object->UnlockRect(); }
	};

	class D3D9Texture : public D3D9Resource<W3DTexture, IDirect3DTexture9>
	{
	public:
		using D3D9Resource::D3D9Resource;

		DWORD GetLevelCount() override { return Object->GetLevelCount(); }
		DWORD SetPriority(DWORD priority) override { return Object->SetPriority(priority); }
		DWORD GetPriority() override { return Object->GetPriority(); }
		HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override { return Object->GetLevelDesc(level, desc); }
		HRESULT GetSurfaceLevel(UINT level, W3DSurface **surface) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->GetSurfaceLevel(level, &object);
			*surface = SUCCEEDED(hr) ? Wrap<D3D9Surface>(object) : nullptr;
			return hr;
		}
		HRESULT LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override { return Object->LockRect(level, locked, rect, flags); }
		HRESULT UnlockRect(UINT level) override { return Object->UnlockRect(level); }
	};

	class D3D9VertexBuffer : public D3D9Resource<W3DVertexBuffer, IDirect3DVertexBuffer9>
	{
	public:
		using D3D9Resource::D3D9Resource;

		HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override { return Object->Lock(offset, size, data, flags); }
		HRESULT Unlock() override { return Object->Unlock(); }
	};

	class D3D9IndexBuffer : public D3D9Resource<W3DIndexBuffer, IDirect3DIndexBuffer9>
	{
	public:
		using D3D9Resource::D3D9Resource;

		HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override { return Object->Lock(offset, size, data, flags); }
		HRESULT Unlock() override { return Object->Unlock(); }
	};

	class D3D9SwapChain : public D3D9Object<W3DSwapChain, IDirect3DSwapChain9>
	{
	public:
		using D3D9Object::D3D9Object;

		HRESULT GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, W3DSurface **surface) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->GetBackBuffer(index, type, &object);
			*surface = SUCCEEDED(hr) ? Wrap<D3D9Surface>(object) : nullptr;
			return hr;
		}
		HRESULT Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty, DWORD flags) override
		{
			return Object->Present(source, dest, window, dirty, flags);
		}
	};

	IDirect3DSurface9 *		Unwrap(W3DSurface *s)			{ return s ? static_cast<D3D9Surface *>(s)->Peek() : nullptr; }
	IDirect3DVertexBuffer9 *Unwrap(W3DVertexBuffer *b)		{ return b ? static_cast<D3D9VertexBuffer *>(b)->Peek() : nullptr; }
	IDirect3DIndexBuffer9 *	Unwrap(W3DIndexBuffer *b)		{ return b ? static_cast<D3D9IndexBuffer *>(b)->Peek() : nullptr; }
	// Only 2D textures exist in this engine.
	IDirect3DTexture9 *		Unwrap(W3DBaseTexture *t)		{ return t ? static_cast<D3D9Texture *>(static_cast<W3DTexture *>(t))->Peek() : nullptr; }

	template <class Wrapper, class Out, class Com>
	HRESULT Wrap_Out(HRESULT hr, Com *object, Out **out)
	{
		*out = SUCCEEDED(hr) ? Wrap<Wrapper>(object) : nullptr;
		return hr;
	}

	class D3D9Device : public D3D9Object<W3DRenderDevice, IDirect3DDevice9>
	{
	public:
		using D3D9Object::D3D9Object;

		HRESULT BeginScene() override { return Object->BeginScene(); }
		HRESULT EndScene() override { return Object->EndScene(); }
		HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override { return Object->Clear(count, rects, flags, color, z, stencil); }
		HRESULT Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty) override { return Object->Present(source, dest, window, dirty); }
		HRESULT TestCooperativeLevel() override { return Object->TestCooperativeLevel(); }
		HRESULT Reset(D3DPRESENT_PARAMETERS *params) override { return Object->Reset(params); }
		HRESULT ValidateDevice(DWORD *passes) override { return Object->ValidateDevice(passes); }

		HRESULT GetDeviceCaps(D3DCAPS9 *caps) override { return Object->GetDeviceCaps(caps); }
		HRESULT SetSoftwareVertexProcessing(BOOL software) override { return Object->SetSoftwareVertexProcessing(software); }
		HRESULT GetDisplayMode(UINT swap_chain, D3DDISPLAYMODE *mode) override { return Object->GetDisplayMode(swap_chain, mode); }
		UINT GetAvailableTextureMem() override { return Object->GetAvailableTextureMem(); }
		HRESULT EvictManagedResources() override { return Object->EvictManagedResources(); }
		void SetGammaRamp(UINT swap_chain, DWORD flags, const D3DGAMMARAMP *ramp) override { Object->SetGammaRamp(swap_chain, flags, ramp); }

		HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, W3DVertexBuffer **buffer, HANDLE *shared) override
		{
			IDirect3DVertexBuffer9 *object = nullptr;
			const HRESULT hr = Object->CreateVertexBuffer(length, usage, fvf, pool, &object, shared);
			return Wrap_Out<D3D9VertexBuffer>(hr, object, buffer);
		}
		HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DIndexBuffer **buffer, HANDLE *shared) override
		{
			IDirect3DIndexBuffer9 *object = nullptr;
			const HRESULT hr = Object->CreateIndexBuffer(length, usage, format, pool, &object, shared);
			return Wrap_Out<D3D9IndexBuffer>(hr, object, buffer);
		}
		HRESULT CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, W3DSurface **surface, HANDLE *shared) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->CreateOffscreenPlainSurface(width, height, format, pool, &object, shared);
			return Wrap_Out<D3D9Surface>(hr, object, surface);
		}
		HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *params, W3DSwapChain **swap_chain) override
		{
			IDirect3DSwapChain9 *object = nullptr;
			const HRESULT hr = Object->CreateAdditionalSwapChain(params, &object);
			*swap_chain = SUCCEEDED(hr) ? new D3D9SwapChain(object) : nullptr;
			return hr;
		}

		HRESULT GetBackBuffer(UINT swap_chain, UINT index, D3DBACKBUFFER_TYPE type, W3DSurface **surface) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->GetBackBuffer(swap_chain, index, type, &object);
			return Wrap_Out<D3D9Surface>(hr, object, surface);
		}
		HRESULT GetFrontBufferData(UINT swap_chain, W3DSurface *dest) override { return Object->GetFrontBufferData(swap_chain, Unwrap(dest)); }
		HRESULT GetRenderTarget(DWORD index, W3DSurface **surface) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->GetRenderTarget(index, &object);
			return Wrap_Out<D3D9Surface>(hr, object, surface);
		}
		HRESULT SetRenderTarget(DWORD index, W3DSurface *surface) override { return Object->SetRenderTarget(index, Unwrap(surface)); }
		HRESULT GetDepthStencilSurface(W3DSurface **surface) override
		{
			IDirect3DSurface9 *object = nullptr;
			const HRESULT hr = Object->GetDepthStencilSurface(&object);
			return Wrap_Out<D3D9Surface>(hr, object, surface);
		}
		HRESULT SetDepthStencilSurface(W3DSurface *surface) override { return Object->SetDepthStencilSurface(Unwrap(surface)); }
		HRESULT GetRenderTargetData(W3DSurface *render_target, W3DSurface *dest) override { return Object->GetRenderTargetData(Unwrap(render_target), Unwrap(dest)); }
		HRESULT UpdateSurface(W3DSurface *source, const RECT *source_rect, W3DSurface *dest, const POINT *dest_point) override
		{
			return Object->UpdateSurface(Unwrap(source), source_rect, Unwrap(dest), dest_point);
		}
		HRESULT UpdateTexture(W3DBaseTexture *source, W3DBaseTexture *dest) override { return Object->UpdateTexture(Unwrap(source), Unwrap(dest)); }

		HRESULT SetViewport(const D3DVIEWPORT9 *viewport) override { return Object->SetViewport(viewport); }
		HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override { return Object->SetTransform(state, matrix); }
		HRESULT GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) override { return Object->GetTransform(state, matrix); }
		HRESULT SetMaterial(const D3DMATERIAL9 *material) override { return Object->SetMaterial(material); }
		HRESULT SetLight(DWORD index, const D3DLIGHT9 *light) override { return Object->SetLight(index, light); }
		HRESULT LightEnable(DWORD index, BOOL enable) override { return Object->LightEnable(index, enable); }
		HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override { return Object->SetRenderState(state, value); }
		HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) override { return Object->SetTextureStageState(stage, state, value); }
		HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value) override { return Object->SetSamplerState(sampler, state, value); }
		HRESULT SetTexture(DWORD stage, W3DBaseTexture *texture) override { return Object->SetTexture(stage, Unwrap(texture)); }
		HRESULT SetNPatchMode(float segments) override { return Object->SetNPatchMode(segments); }

		HRESULT SetStreamSource(UINT stream, W3DVertexBuffer *buffer, UINT offset, UINT stride) override { return Object->SetStreamSource(stream, Unwrap(buffer), offset, stride); }
		HRESULT SetIndices(W3DIndexBuffer *buffer) override { return Object->SetIndices(Unwrap(buffer)); }
		HRESULT SetFVF(DWORD fvf) override { return Object->SetFVF(fvf); }
		HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count, UINT start_index, UINT primitive_count) override
		{
			return Object->DrawIndexedPrimitive(type, base_vertex, min_index, vertex_count, start_index, primitive_count);
		}

		HRESULT Create_Texture(UINT width, UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, W3DTexture **texture) override
		{
			IDirect3DTexture9 *object = nullptr;
			const HRESULT hr = D3DXCreateTexture(Object, width, height, mip_levels, usage, format, pool, &object);
			return Wrap_Out<D3D9Texture>(hr, object, texture);
		}
		HRESULT Load_Surface_From_Surface(W3DSurface *dest, const RECT *dest_rect, W3DSurface *source, const RECT *source_rect, W3DImageFilter filter) override
		{
			return D3DXLoadSurfaceFromSurface(Unwrap(dest), nullptr, dest_rect, Unwrap(source), nullptr, source_rect, filter, 0);
		}
		HRESULT Filter_Texture(W3DBaseTexture *texture, W3DImageFilter filter) override
		{
			return D3DXFilterTexture(Unwrap(texture), nullptr, 0, filter);
		}
		HRESULT Save_Surface_To_TGA(const char *filename, W3DSurface *surface) override
		{
			return D3DXSaveSurfaceToFileA(filename, D3DXIFF_TGA, Unwrap(surface), nullptr, nullptr);
		}
	};

	static_assert(DWORD(W3D_FILTER_NONE) == D3DX_FILTER_NONE && DWORD(W3D_FILTER_POINT) == D3DX_FILTER_POINT &&
		DWORD(W3D_FILTER_LINEAR) == D3DX_FILTER_LINEAR && DWORD(W3D_FILTER_TRIANGLE) == D3DX_FILTER_TRIANGLE &&
		DWORD(W3D_FILTER_BOX) == D3DX_FILTER_BOX, "W3DImageFilter must match D3DX_FILTER_*");

	class D3D9RenderSystem : public D3D9Object<W3DRenderSystem, IDirect3D9>
	{
	public:
		D3D9RenderSystem(IDirect3D9 *object, SharedObject *library) : D3D9Object(object), Library(library) {}

		UINT GetAdapterCount() override { return Object->GetAdapterCount(); }
		HRESULT GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) override { return Object->GetAdapterIdentifier(adapter, flags, identifier); }
		UINT GetAdapterModeCount(UINT adapter, D3DFORMAT format) override { return Object->GetAdapterModeCount(adapter, format); }
		HRESULT EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *display_mode) override { return Object->EnumAdapterModes(adapter, format, mode, display_mode); }
		HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override { return Object->GetAdapterDisplayMode(adapter, mode); }
		HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override { return Object->GetDeviceCaps(adapter, type, caps); }
		HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT check_format) override
		{
			return Object->CheckDeviceFormat(adapter, type, adapter_format, usage, resource_type, check_format);
		}
		HRESULT CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, D3DFORMAT render_target_format, D3DFORMAT depth_format) override
		{
			return Object->CheckDepthStencilMatch(adapter, type, adapter_format, render_target_format, depth_format);
		}
		HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *params, W3DRenderDevice **device) override
		{
			IDirect3DDevice9 *object = nullptr;
			const HRESULT hr = Object->CreateDevice(adapter, type, window, flags, params, &object);
			*device = SUCCEEDED(hr) ? new D3D9Device(object) : nullptr;
			return hr;
		}
		const char *Get_Backend_Name() const override { return "Direct3D 9"; }

	protected:
		~D3D9RenderSystem() override
		{
			// The interface must be released before its DLL is unloaded.
			Object->Release();
			Object = nullptr;
			delete Library;
		}

	private:
		SharedObject *	Library;
	};
}

W3DRenderSystem *W3D_Create_D3D9_Render_System()
{
	SharedObject *library = SharedObject::LoadObject(D3D9_Object_Name());
	if (library == nullptr) {
		WWRELEASE_WARNING(("D3D9: could not load %s\n", D3D9_Object_Name()));
		return nullptr;
	}

	typedef IDirect3D9 *(WINAPI *CreateFunction)(UINT);
	CreateFunction create = reinterpret_cast<CreateFunction>(library->LoadFunction("Direct3DCreate9"));
	IDirect3D9 *d3d = create ? create(D3D_SDK_VERSION) : nullptr;
	if (d3d == nullptr) {
		delete library;
		return nullptr;
	}
	return new D3D9RenderSystem(d3d, library);
}

W3DRenderSystem *W3D_Wrap_D3D9_Render_System(IDirect3D9 *d3d)
{
	if (d3d == nullptr) {
		return nullptr;
	}
	d3d->AddRef();
	return new D3D9RenderSystem(d3d, nullptr);
}

const char *W3D_D3D9_Error_String(HRESULT result)
{
	return DXGetErrorStringA(result);
}

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
** Native Direct3D 12 device layer.
**
** Owns the device, the direct queue, a flip-model swap chain and the per-frame
** resources the renderer needs (command allocators, fences, an upload ring and
** descriptor heaps). The direct command list is always open: work recorded outside
** Begin_Frame()/End_Frame() (texture uploads during level loads, for example) is
** submitted with the next frame or an explicit Flush().
*/

#pragma once

#ifndef D3D12DEVICE_H
#define D3D12DEVICE_H

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

const char *D3D12_Result_Name(HRESULT hr);

/*
** CPU-visible linear allocator backed by UPLOAD heap pages. Allocations made while a
** frame slot is current stay valid until that slot's fence completes, after which the
** pages are recycled.
*/
class D3D12UploadRingClass
{
public:
	struct Allocation
	{
		void *							CPU = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS	GPU = 0;
		ID3D12Resource *				Resource = nullptr;
		uint64_t							Offset = 0;
	};

	static constexpr uint64_t DEFAULT_PAGE_SIZE = 8ull * 1024 * 1024;

	bool			Init(ID3D12Device *device, unsigned frame_count);
	void			Shutdown();

	// Recycle the pages owned by 'frame_slot'. The caller guarantees the GPU is done with them.
	void			Begin_Frame(unsigned frame_slot);
	bool			Allocate(uint64_t size, uint64_t alignment, Allocation &out);

	uint64_t		Bytes_Used_This_Frame() const { return BytesThisFrame; }

private:
	struct Page
	{
		ComPtr<ID3D12Resource>	Resource;
		uint8_t *					CPU = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS	GPU = 0;
		uint64_t						Size = 0;
		uint64_t						Used = 0;
	};

	bool			Create_Page(uint64_t size, Page &page);

	ID3D12Device *						Device = nullptr;
	std::vector<std::vector<Page>>	InFlight;		// pages per frame slot, last one is current
	std::vector<Page>					FreePages;
	unsigned								CurrentSlot = 0;
	uint64_t								BytesThisFrame = 0;
};

/*
** Fixed-size descriptor heap with a free list. Used for non-shader-visible RTV, DSV and
** SRV descriptors that live as long as the resource they describe.
*/
class D3D12DescriptorHeapClass
{
public:
	bool			Init(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned count, bool shader_visible);
	void			Shutdown();

	// Returns UINT32_MAX when the heap is full.
	unsigned		Allocate();
	void			Free(unsigned index);

	D3D12_CPU_DESCRIPTOR_HANDLE	CPU(unsigned index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE	GPU(unsigned index) const;
	ID3D12DescriptorHeap *			Heap() const { return HeapObject.Get(); }
	unsigned								Increment() const { return DescriptorSize; }
	unsigned								Capacity() const { return Count; }

private:
	ComPtr<ID3D12DescriptorHeap>	HeapObject;
	D3D12_CPU_DESCRIPTOR_HANDLE	CPUStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE	GPUStart = {};
	unsigned								DescriptorSize = 0;
	unsigned								Count = 0;
	std::vector<unsigned>			FreeList;
};

struct D3D12DeviceInitParams
{
	HWND				Window = nullptr;
	IDXGIAdapter1 *	Adapter = nullptr;		// Use this adapter; null picks the high-performance GPU.
	unsigned			Width = 0;
	unsigned			Height = 0;
	bool				UseWarp = false;			// Software adapter, for tests and CI.
	bool				DebugLayer = false;		// Enable the D3D12 debug layer if installed.
	DXGI_FORMAT		BackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT		DepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

class D3D12DeviceClass
{
public:
	static constexpr unsigned FRAMES_IN_FLIGHT = 2;
	static constexpr unsigned BACK_BUFFER_COUNT = 3;

	D3D12DeviceClass() = default;
	~D3D12DeviceClass() { Shutdown(); }
	D3D12DeviceClass(const D3D12DeviceClass &) = delete;
	D3D12DeviceClass &operator=(const D3D12DeviceClass &) = delete;

	bool			Init(const D3D12DeviceInitParams &params);
	void			Shutdown();
	bool			Is_Initialized() const { return DeviceObject != nullptr; }

	// Recreate the swap chain buffers and depth buffer at a new size.
	bool			Resize(unsigned width, unsigned height);

	// Bind the current back buffer and depth buffer as the render target.
	void			Begin_Frame();
	void			Clear(bool clear_color, bool clear_depth_stencil, const float rgba[4], float depth, uint8_t stencil);
	// Submit the frame and optionally present it. Returns false if the device was lost.
	bool			End_Frame(bool present, unsigned sync_interval);

	// Submit the recorded work, wait for it and reopen the command list.
	void			Flush();
	void			Wait_For_GPU();

	// Copy the current back buffer to CPU memory as tightly packed 32-bit pixels in the
	// back buffer format. Must be called between Begin_Frame and End_Frame; flushes the
	// GPU and leaves the back buffer bound as the render target again.
	bool			Read_Back_Buffer(std::vector<uint32_t> &pixels, unsigned &width, unsigned &height);

	// Resource helpers
	void			Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	void			Bind_Back_Buffer();

	ID3D12Device *						Device() const { return DeviceObject.Get(); }
	ID3D12GraphicsCommandList *	Command_List() const { return CommandList.Get(); }
	ID3D12CommandQueue *				Queue() const { return QueueObject.Get(); }
	IDXGIAdapter1 *					Adapter() const { return AdapterObject.Get(); }
	const DXGI_ADAPTER_DESC1 &		Adapter_Desc() const { return AdapterDesc; }

	unsigned								Frame_Slot() const { return FrameSlot; }
	uint64_t								Frame_Number() const { return FrameNumber; }
	unsigned								Width() const { return BufferWidth; }
	unsigned								Height() const { return BufferHeight; }
	DXGI_FORMAT							Back_Buffer_Format() const { return BackBufferFormat; }
	DXGI_FORMAT							Depth_Format() const { return DepthFormat; }
	bool									Is_In_Frame() const { return InFrame; }
	bool									Supports_Tearing() const { return AllowTearing; }

	D3D12UploadRingClass &				Upload() { return UploadRing; }
	D3D12DescriptorHeapClass &			RTV_Heap() { return RTVHeap; }
	D3D12DescriptorHeapClass &			DSV_Heap() { return DSVHeap; }
	// Shader-visible heaps; descriptors live as long as their resource.
	D3D12DescriptorHeapClass &			SRV_Heap() { return SRVHeap; }
	D3D12DescriptorHeapClass &			Sampler_Heap() { return SamplerHeap; }

	D3D12_CPU_DESCRIPTOR_HANDLE		Back_Buffer_RTV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE		Depth_DSV() const;

	// Keep 'resource' alive until the GPU has finished the work recorded so far.
	void									Defer_Release(ComPtr<ID3D12Resource> resource);
	// Return an SRV heap slot once the GPU has finished the work recorded so far.
	void									Defer_Free_SRV(unsigned index);

	// True after a device removal; the device cannot be used any more.
	bool									Is_Lost() const { return DeviceLost; }

	// Changes every time the command list is reset; all bindings must be set again.
	uint64_t								List_Epoch() const { return ListEpoch; }

private:
	bool			Create_Device(const D3D12DeviceInitParams &params);
	bool			Create_Swap_Chain(HWND window);
	bool			Create_Size_Dependent_Resources();
	void			Release_Size_Dependent_Resources();
	bool			Submit();
	void			Wait_For_Fence(uint64_t value);
	void			Reopen_Command_List();
	void			Release_Deferred(bool all);
	bool			Check_Device_Removed(HRESULT hr, const char *where);

	ComPtr<IDXGIFactory4>					Factory;
	ComPtr<IDXGIAdapter1>					AdapterObject;
	DXGI_ADAPTER_DESC1						AdapterDesc = {};
	ComPtr<ID3D12Device>						DeviceObject;
	ComPtr<ID3D12CommandQueue>				QueueObject;
	ComPtr<IDXGISwapChain3>					SwapChain;
	ComPtr<ID3D12CommandAllocator>		Allocators[FRAMES_IN_FLIGHT];
	ComPtr<ID3D12GraphicsCommandList>	CommandList;
	ComPtr<ID3D12Fence>						Fence;
	HANDLE										FenceEvent = nullptr;
	uint64_t										NextFenceValue = 1;
	uint64_t										SlotFenceValues[FRAMES_IN_FLIGHT] = {};

	ComPtr<ID3D12Resource>					BackBuffers[BACK_BUFFER_COUNT];
	unsigned										BackBufferRTV[BACK_BUFFER_COUNT] = {};
	ComPtr<ID3D12Resource>					DepthBuffer;
	unsigned										DepthDSV = UINT32_MAX;

	D3D12DescriptorHeapClass				RTVHeap;
	D3D12DescriptorHeapClass				DSVHeap;
	D3D12DescriptorHeapClass				SRVHeap;
	D3D12DescriptorHeapClass				SamplerHeap;
	D3D12UploadRingClass						UploadRing;

	struct DeferredRelease
	{
		uint64_t						FenceValue;
		ComPtr<ID3D12Resource>	Resource;
		unsigned						SRV;		// UINT32_MAX unless this frees an SRV slot
	};
	std::vector<DeferredRelease>			DeferredReleases;

	DXGI_FORMAT									BackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT									DepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	unsigned										BufferWidth = 0;
	unsigned										BufferHeight = 0;
	unsigned										FrameSlot = 0;
	uint64_t										FrameNumber = 0;
	uint64_t										ListEpoch = 0;
	bool											AllowTearing = false;
	bool											InFrame = false;
	bool											DeviceLost = false;
};

#endif

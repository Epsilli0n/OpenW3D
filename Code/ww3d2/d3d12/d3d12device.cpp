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

#include "d3d12device.h"

#include "wwdebug.h"

#include <algorithm>
#include <cstring>

namespace
{
	constexpr unsigned RTV_HEAP_SIZE = 64;
	constexpr unsigned DSV_HEAP_SIZE = 16;
	constexpr unsigned SRV_HEAP_SIZE = 65536;
	constexpr unsigned SAMPLER_HEAP_SIZE = 2048;

	uint64_t Align_Up(uint64_t value, uint64_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	D3D12_HEAP_PROPERTIES Heap_Properties(D3D12_HEAP_TYPE type)
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = type;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.CreationNodeMask = 1;
		props.VisibleNodeMask = 1;
		return props;
	}

	D3D12_RESOURCE_DESC Buffer_Desc(uint64_t size)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		return desc;
	}
}

const char *D3D12_Result_Name(HRESULT hr)
{
	switch (hr) {
	case S_OK:									return "S_OK";
	case E_FAIL:								return "E_FAIL";
	case E_INVALIDARG:						return "E_INVALIDARG";
	case E_OUTOFMEMORY:						return "E_OUTOFMEMORY";
	case E_NOTIMPL:							return "E_NOTIMPL";
	case DXGI_ERROR_DEVICE_REMOVED:		return "DXGI_ERROR_DEVICE_REMOVED";
	case DXGI_ERROR_DEVICE_HUNG:			return "DXGI_ERROR_DEVICE_HUNG";
	case DXGI_ERROR_DEVICE_RESET:			return "DXGI_ERROR_DEVICE_RESET";
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR:	return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
	case DXGI_ERROR_INVALID_CALL:			return "DXGI_ERROR_INVALID_CALL";
	case DXGI_ERROR_UNSUPPORTED:			return "DXGI_ERROR_UNSUPPORTED";
	case DXGI_ERROR_NOT_FOUND:				return "DXGI_ERROR_NOT_FOUND";
	default:										return "unknown HRESULT";
	}
}

#define D3D12_CHECK(expr, what)																	\
	do {																									\
		HRESULT _hr = (expr);																		\
		if (FAILED(_hr)) {																			\
			WWRELEASE_WARNING(("D3D12: %s failed: %s (0x%08x)\n", what, D3D12_Result_Name(_hr), (unsigned)_hr));	\
			return false;																				\
		}																									\
	} while (0)

/***********************************************************************************************
** D3D12UploadRingClass
***********************************************************************************************/

bool D3D12UploadRingClass::Init(ID3D12Device *device, unsigned frame_count)
{
	Device = device;
	InFlight.assign(frame_count, {});
	CurrentSlot = 0;
	BytesThisFrame = 0;
	return true;
}

void D3D12UploadRingClass::Shutdown()
{
	InFlight.clear();
	FreePages.clear();
	Device = nullptr;
}

bool D3D12UploadRingClass::Create_Page(uint64_t size, Page &page)
{
	const D3D12_HEAP_PROPERTIES heap = Heap_Properties(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC desc = Buffer_Desc(size);
	D3D12_CHECK(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&page.Resource)), "upload page");

	const D3D12_RANGE no_read = { 0, 0 };
	void *cpu = nullptr;
	D3D12_CHECK(page.Resource->Map(0, &no_read, &cpu), "map upload page");
	page.CPU = static_cast<uint8_t *>(cpu);
	page.GPU = page.Resource->GetGPUVirtualAddress();
	page.Size = size;
	page.Used = 0;
	return true;
}

void D3D12UploadRingClass::Begin_Frame(unsigned frame_slot)
{
	CurrentSlot = frame_slot;
	BytesThisFrame = 0;
	std::vector<Page> &pages = InFlight[CurrentSlot];
	for (Page &page : pages) {
		page.Used = 0;
		// Oversized one-off pages are not worth keeping around.
		if (page.Size == DEFAULT_PAGE_SIZE) {
			FreePages.push_back(std::move(page));
		}
	}
	pages.clear();
}

bool D3D12UploadRingClass::Allocate(uint64_t size, uint64_t alignment, Allocation &out)
{
	if (alignment == 0) {
		alignment = 1;
	}

	std::vector<Page> &pages = InFlight[CurrentSlot];
	if (!pages.empty()) {
		Page &page = pages.back();
		const uint64_t offset = Align_Up(page.Used, alignment);
		if (offset + size <= page.Size) {
			page.Used = offset + size;
			out.CPU = page.CPU + offset;
			out.GPU = page.GPU + offset;
			out.Resource = page.Resource.Get();
			out.Offset = offset;
			BytesThisFrame += size;
			return true;
		}
	}

	Page page;
	if (size <= DEFAULT_PAGE_SIZE && !FreePages.empty()) {
		page = std::move(FreePages.back());
		FreePages.pop_back();
	} else if (!Create_Page(std::max(DEFAULT_PAGE_SIZE, Align_Up(size, 65536)), page)) {
		return false;
	}

	page.Used = size;
	out.CPU = page.CPU;
	out.GPU = page.GPU;
	out.Resource = page.Resource.Get();
	out.Offset = 0;
	BytesThisFrame += size;
	pages.push_back(std::move(page));
	return true;
}

/***********************************************************************************************
** D3D12DescriptorHeapClass
***********************************************************************************************/

bool D3D12DescriptorHeapClass::Init(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned count, bool shader_visible)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = type;
	desc.NumDescriptors = count;
	desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	D3D12_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&HeapObject)), "descriptor heap");

	CPUStart = HeapObject->GetCPUDescriptorHandleForHeapStart();
	if (shader_visible) {
		GPUStart = HeapObject->GetGPUDescriptorHandleForHeapStart();
	}
	DescriptorSize = device->GetDescriptorHandleIncrementSize(type);
	Count = count;

	FreeList.clear();
	FreeList.reserve(count);
	for (unsigned i = count; i > 0; --i) {
		FreeList.push_back(i - 1);
	}
	return true;
}

void D3D12DescriptorHeapClass::Shutdown()
{
	HeapObject.Reset();
	FreeList.clear();
	Count = 0;
}

unsigned D3D12DescriptorHeapClass::Allocate()
{
	if (FreeList.empty()) {
		WWRELEASE_WARNING(("D3D12: descriptor heap exhausted (%u descriptors)\n", Count));
		return UINT32_MAX;
	}
	const unsigned index = FreeList.back();
	FreeList.pop_back();
	return index;
}

void D3D12DescriptorHeapClass::Free(unsigned index)
{
	if (index < Count) {
		FreeList.push_back(index);
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapClass::CPU(unsigned index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = CPUStart;
	handle.ptr += static_cast<SIZE_T>(index) * DescriptorSize;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapClass::GPU(unsigned index) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle = GPUStart;
	handle.ptr += static_cast<UINT64>(index) * DescriptorSize;
	return handle;
}

/***********************************************************************************************
** D3D12DeviceClass
***********************************************************************************************/

bool D3D12DeviceClass::Init(const D3D12DeviceInitParams &params)
{
	if (Is_Initialized()) {
		Shutdown();
	}

	BackBufferFormat = params.BackBufferFormat;
	DepthFormat = params.DepthFormat;
	BufferWidth = std::max(1u, params.Width);
	BufferHeight = std::max(1u, params.Height);
	DeviceLost = false;

	if (!Create_Device(params)) {
		Shutdown();
		return false;
	}

	if (!RTVHeap.Init(DeviceObject.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_SIZE, false) ||
		!DSVHeap.Init(DeviceObject.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_HEAP_SIZE, false) ||
		!SRVHeap.Init(DeviceObject.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_HEAP_SIZE, true) ||
		!SamplerHeap.Init(DeviceObject.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SAMPLER_HEAP_SIZE, true) ||
		!UploadRing.Init(DeviceObject.Get(), FRAMES_IN_FLIGHT)) {
		Shutdown();
		return false;
	}

	if (params.Window != nullptr && !Create_Swap_Chain(params.Window)) {
		Shutdown();
		return false;
	}

	if (!Create_Size_Dependent_Resources()) {
		Shutdown();
		return false;
	}

	FrameSlot = 0;
	FrameNumber = 0;
	UploadRing.Begin_Frame(FrameSlot);
	Reopen_Command_List();

	char name[128] = {};
	WideCharToMultiByte(CP_UTF8, 0, AdapterDesc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
	WWRELEASE_SAY(("D3D12: device created on '%s' (%ux%u)\n", name, BufferWidth, BufferHeight));
	return true;
}

bool D3D12DeviceClass::Create_Device(const D3D12DeviceInitParams &params)
{
	UINT factory_flags = 0;
	if (params.DebugLayer) {
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
			debug->EnableDebugLayer();
			factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
			WWRELEASE_SAY(("D3D12: debug layer enabled\n"));
		} else {
			WWRELEASE_WARNING(("D3D12: debug layer requested but not installed\n"));
		}
	}

	D3D12_CHECK(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&Factory)), "CreateDXGIFactory2");

	ComPtr<IDXGIFactory5> factory5;
	if (SUCCEEDED(Factory.As(&factory5))) {
		BOOL tearing = FALSE;
		if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)))) {
			AllowTearing = (tearing != FALSE);
		}
	}

	if (params.Adapter != nullptr) {
		AdapterObject = params.Adapter;
		D3D12_CHECK(D3D12CreateDevice(AdapterObject.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&DeviceObject)), "D3D12CreateDevice");
	} else if (params.UseWarp) {
		D3D12_CHECK(Factory->EnumWarpAdapter(IID_PPV_ARGS(&AdapterObject)), "EnumWarpAdapter");
		D3D12_CHECK(D3D12CreateDevice(AdapterObject.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&DeviceObject)), "D3D12CreateDevice (WARP)");
	} else {
		ComPtr<IDXGIFactory6> factory6;
		Factory.As(&factory6);
		for (UINT index = 0; ; ++index) {
			ComPtr<IDXGIAdapter1> adapter;
			HRESULT hr = factory6
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
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				continue;
			}
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&DeviceObject)))) {
				AdapterObject = adapter;
				break;
			}
		}
		if (!DeviceObject) {
			WWRELEASE_WARNING(("D3D12: no hardware adapter supports feature level 11_0\n"));
			return false;
		}
	}
	AdapterObject->GetDesc1(&AdapterDesc);

	if (params.DebugLayer) {
		// Breaking without a debugger attached would just crash the process.
		ComPtr<ID3D12InfoQueue> info;
		if (IsDebuggerPresent() && SUCCEEDED(DeviceObject.As(&info))) {
			info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		}
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	D3D12_CHECK(DeviceObject->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&QueueObject)), "CreateCommandQueue");

	for (unsigned i = 0; i < FRAMES_IN_FLIGHT; ++i) {
		D3D12_CHECK(DeviceObject->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocators[i])), "CreateCommandAllocator");
	}
	D3D12_CHECK(DeviceObject->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocators[0].Get(), nullptr, IID_PPV_ARGS(&CommandList)), "CreateCommandList");
	// Created open; close it so Reopen_Command_List() has a uniform starting point.
	CommandList->Close();

	D3D12_CHECK(DeviceObject->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)), "CreateFence");
	FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (FenceEvent == nullptr) {
		return false;
	}
	NextFenceValue = 1;
	for (uint64_t &value : SlotFenceValues) {
		value = 0;
	}
	return true;
}

bool D3D12DeviceClass::Create_Swap_Chain(HWND window)
{
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = BufferWidth;
	desc.Height = BufferHeight;
	desc.Format = BackBufferFormat;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = BACK_BUFFER_COUNT;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.Flags = AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swap_chain;
	D3D12_CHECK(Factory->CreateSwapChainForHwnd(QueueObject.Get(), window, &desc, nullptr, nullptr, &swap_chain), "CreateSwapChainForHwnd");
	D3D12_CHECK(swap_chain.As(&SwapChain), "IDXGISwapChain3");

	// The engine handles window mode changes itself.
	Factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
	return true;
}

bool D3D12DeviceClass::Create_Size_Dependent_Resources()
{
	if (SwapChain) {
		for (unsigned i = 0; i < BACK_BUFFER_COUNT; ++i) {
			D3D12_CHECK(SwapChain->GetBuffer(i, IID_PPV_ARGS(&BackBuffers[i])), "GetBuffer");
			BackBufferRTV[i] = RTVHeap.Allocate();
			DeviceObject->CreateRenderTargetView(BackBuffers[i].Get(), nullptr, RTVHeap.CPU(BackBufferRTV[i]));
		}
	} else {
		// Headless (tests): a single offscreen colour buffer stands in for the swap chain.
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = BufferWidth;
		desc.Height = BufferHeight;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = BackBufferFormat;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		const D3D12_HEAP_PROPERTIES heap = Heap_Properties(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_CHECK(DeviceObject->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&BackBuffers[0])), "offscreen back buffer");
		BackBufferRTV[0] = RTVHeap.Allocate();
		DeviceObject->CreateRenderTargetView(BackBuffers[0].Get(), nullptr, RTVHeap.CPU(BackBufferRTV[0]));
	}

	D3D12_RESOURCE_DESC depth_desc = {};
	depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depth_desc.Width = BufferWidth;
	depth_desc.Height = BufferHeight;
	depth_desc.DepthOrArraySize = 1;
	depth_desc.MipLevels = 1;
	depth_desc.Format = DepthFormat;
	depth_desc.SampleDesc.Count = 1;
	depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DepthFormat;
	clear.DepthStencil.Depth = 1.0f;
	clear.DepthStencil.Stencil = 0;

	const D3D12_HEAP_PROPERTIES heap = Heap_Properties(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_CHECK(DeviceObject->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&DepthBuffer)), "depth buffer");
	DepthDSV = DSVHeap.Allocate();
	DeviceObject->CreateDepthStencilView(DepthBuffer.Get(), nullptr, DSVHeap.CPU(DepthDSV));
	return true;
}

void D3D12DeviceClass::Release_Size_Dependent_Resources()
{
	for (unsigned i = 0; i < BACK_BUFFER_COUNT; ++i) {
		if (BackBuffers[i]) {
			RTVHeap.Free(BackBufferRTV[i]);
			BackBuffers[i].Reset();
		}
	}
	if (DepthBuffer) {
		DSVHeap.Free(DepthDSV);
		DepthDSV = UINT32_MAX;
		DepthBuffer.Reset();
	}
}

void D3D12DeviceClass::Shutdown()
{
	if (DeviceObject && QueueObject && Fence) {
		Wait_For_GPU();
	}
	Release_Deferred(true);
	Release_Size_Dependent_Resources();

	UploadRing.Shutdown();
	SamplerHeap.Shutdown();
	SRVHeap.Shutdown();
	DSVHeap.Shutdown();
	RTVHeap.Shutdown();

	SwapChain.Reset();
	CommandList.Reset();
	for (ComPtr<ID3D12CommandAllocator> &allocator : Allocators) {
		allocator.Reset();
	}
	Fence.Reset();
	if (FenceEvent != nullptr) {
		CloseHandle(FenceEvent);
		FenceEvent = nullptr;
	}
	QueueObject.Reset();
	DeviceObject.Reset();
	AdapterObject.Reset();
	Factory.Reset();
	InFrame = false;
}

bool D3D12DeviceClass::Resize(unsigned width, unsigned height)
{
	if (!Is_Initialized()) {
		return false;
	}
	width = std::max(1u, width);
	height = std::max(1u, height);
	if (width == BufferWidth && height == BufferHeight) {
		return true;
	}

	WWASSERT(!InFrame);
	Flush();
	Release_Size_Dependent_Resources();

	BufferWidth = width;
	BufferHeight = height;
	if (SwapChain) {
		D3D12_CHECK(SwapChain->ResizeBuffers(BACK_BUFFER_COUNT, width, height, BackBufferFormat,
			AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0), "ResizeBuffers");
	}
	return Create_Size_Dependent_Resources();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceClass::Back_Buffer_RTV() const
{
	const unsigned index = SwapChain ? SwapChain->GetCurrentBackBufferIndex() : 0;
	return RTVHeap.CPU(BackBufferRTV[index]);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceClass::Depth_DSV() const
{
	return DSVHeap.CPU(DepthDSV);
}

void D3D12DeviceClass::Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	if (before == after) {
		return;
	}
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	CommandList->ResourceBarrier(1, &barrier);
}

void D3D12DeviceClass::Bind_Back_Buffer()
{
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = Back_Buffer_RTV();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = Depth_DSV();
	CommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, float(BufferWidth), float(BufferHeight), 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, LONG(BufferWidth), LONG(BufferHeight) };
	CommandList->RSSetViewports(1, &viewport);
	CommandList->RSSetScissorRects(1, &scissor);
}

void D3D12DeviceClass::Begin_Frame()
{
	WWASSERT(Is_Initialized());
	WWASSERT(!InFrame);
	InFrame = true;

	ID3D12Resource *back_buffer = BackBuffers[SwapChain ? SwapChain->GetCurrentBackBufferIndex() : 0].Get();
	Transition(back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	Bind_Back_Buffer();
}

void D3D12DeviceClass::Clear(bool clear_color, bool clear_depth_stencil, const float rgba[4], float depth, uint8_t stencil)
{
	if (clear_color) {
		CommandList->ClearRenderTargetView(Back_Buffer_RTV(), rgba, 0, nullptr);
	}
	if (clear_depth_stencil) {
		CommandList->ClearDepthStencilView(Depth_DSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
	}
}

bool D3D12DeviceClass::End_Frame(bool present, unsigned sync_interval)
{
	WWASSERT(InFrame);
	InFrame = false;

	ID3D12Resource *back_buffer = BackBuffers[SwapChain ? SwapChain->GetCurrentBackBufferIndex() : 0].Get();
	Transition(back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	if (!Submit()) {
		return false;
	}

	if (present && SwapChain) {
		const UINT flags = (sync_interval == 0 && AllowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		const HRESULT hr = SwapChain->Present(sync_interval, flags);
		if (!Check_Device_Removed(hr, "Present")) {
			return false;
		}
	}

	// Mark the slot as busy until everything submitted so far completes.
	SlotFenceValues[FrameSlot] = NextFenceValue;
	QueueObject->Signal(Fence.Get(), NextFenceValue);
	++NextFenceValue;

	FrameSlot = (FrameSlot + 1) % FRAMES_IN_FLIGHT;
	++FrameNumber;
	Wait_For_Fence(SlotFenceValues[FrameSlot]);

	Allocators[FrameSlot]->Reset();
	UploadRing.Begin_Frame(FrameSlot);
	Release_Deferred(false);
	Reopen_Command_List();
	return !DeviceLost;
}

bool D3D12DeviceClass::Submit()
{
	const HRESULT hr = CommandList->Close();
	if (FAILED(hr)) {
		WWRELEASE_WARNING(("D3D12: command list close failed: %s\n", D3D12_Result_Name(hr)));
		Check_Device_Removed(DeviceObject->GetDeviceRemovedReason(), "Close");
		return false;
	}
	ID3D12CommandList *lists[] = { CommandList.Get() };
	QueueObject->ExecuteCommandLists(1, lists);
	return true;
}

void D3D12DeviceClass::Reopen_Command_List()
{
	CommandList->Reset(Allocators[FrameSlot].Get(), nullptr);
	ID3D12DescriptorHeap *heaps[] = { SRVHeap.Heap(), SamplerHeap.Heap() };
	CommandList->SetDescriptorHeaps(2, heaps);
}

void D3D12DeviceClass::Wait_For_Fence(uint64_t value)
{
	if (value == 0 || Fence->GetCompletedValue() >= value) {
		return;
	}
	Fence->SetEventOnCompletion(value, FenceEvent);
	WaitForSingleObject(FenceEvent, INFINITE);
}

void D3D12DeviceClass::Wait_For_GPU()
{
	QueueObject->Signal(Fence.Get(), NextFenceValue);
	Wait_For_Fence(NextFenceValue);
	++NextFenceValue;
}

void D3D12DeviceClass::Flush()
{
	if (!Submit()) {
		return;
	}
	Wait_For_GPU();
	Release_Deferred(false);
	Reopen_Command_List();
	if (InFrame) {
		Bind_Back_Buffer();
	}
}

void D3D12DeviceClass::Defer_Release(ComPtr<ID3D12Resource> resource)
{
	if (resource) {
		DeferredReleases.push_back({ NextFenceValue, std::move(resource), UINT32_MAX });
	}
}

void D3D12DeviceClass::Defer_Free_SRV(unsigned index)
{
	if (index != UINT32_MAX) {
		DeferredReleases.push_back({ NextFenceValue, nullptr, index });
	}
}

void D3D12DeviceClass::Release_Deferred(bool all)
{
	if (DeferredReleases.empty()) {
		return;
	}
	const uint64_t completed = (all || !Fence) ? UINT64_MAX : Fence->GetCompletedValue();
	DeferredReleases.erase(
		std::remove_if(DeferredReleases.begin(), DeferredReleases.end(),
			[this, completed](const DeferredRelease &entry) {
				if (entry.FenceValue > completed) {
					return false;
				}
				if (entry.SRV != UINT32_MAX) {
					SRVHeap.Free(entry.SRV);
				}
				return true;
			}),
		DeferredReleases.end());
}

bool D3D12DeviceClass::Check_Device_Removed(HRESULT hr, const char *where)
{
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
		const HRESULT reason = DeviceObject->GetDeviceRemovedReason();
		WWRELEASE_ERROR(("D3D12: device lost during %s: %s (reason %s)\n", where, D3D12_Result_Name(hr), D3D12_Result_Name(reason)));
		DeviceLost = true;
		return false;
	}
	if (FAILED(hr)) {
		WWRELEASE_WARNING(("D3D12: %s failed: %s (0x%08x)\n", where, D3D12_Result_Name(hr), (unsigned)hr));
	}
	return true;
}

bool D3D12DeviceClass::Read_Back_Buffer(std::vector<uint32_t> &pixels, unsigned &width, unsigned &height)
{
	WWASSERT(InFrame);
	ID3D12Resource *back_buffer = BackBuffers[SwapChain ? SwapChain->GetCurrentBackBufferIndex() : 0].Get();
	const D3D12_RESOURCE_DESC desc = back_buffer->GetDesc();

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT64 total = 0;
	DeviceObject->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);

	ComPtr<ID3D12Resource> readback;
	const D3D12_HEAP_PROPERTIES heap = Heap_Properties(D3D12_HEAP_TYPE_READBACK);
	const D3D12_RESOURCE_DESC buffer_desc = Buffer_Desc(total);
	D3D12_CHECK(DeviceObject->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "readback buffer");

	Transition(back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = readback.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = back_buffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;
	CommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	Transition(back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	Flush();

	width = unsigned(desc.Width);
	height = desc.Height;
	pixels.resize(size_t(width) * height);

	const D3D12_RANGE read_range = { 0, SIZE_T(total) };
	void *mapped = nullptr;
	D3D12_CHECK(readback->Map(0, &read_range, &mapped), "map readback");
	const uint8_t *src_rows = static_cast<const uint8_t *>(mapped) + footprint.Offset;
	for (unsigned y = 0; y < height; ++y) {
		memcpy(&pixels[size_t(y) * width], src_rows + size_t(y) * footprint.Footprint.RowPitch, size_t(width) * 4);
	}
	const D3D12_RANGE no_write = { 0, 0 };
	readback->Unmap(0, &no_write);
	return true;
}

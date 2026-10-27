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

// End-to-end checks for the D3D12 device layer. Runs on the WARP adapter by default so
// it works on machines without a GPU; set W3D_D3D12_TEST_HARDWARE=1 to use the GPU.

#include "d3d12device.h"

#include "g_d3d12_smoke_vs.h"
#include "g_d3d12_smoke_ps.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
constexpr uint32_t CLEAR_BLUE = 0xFF0000FFu;	// B8G8R8A8 in memory, read as a little-endian uint32
constexpr uint32_t PURE_RED = 0xFFFF0000u;
constexpr uint32_t HALF_RED = 0xFF800000u;

bool Expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool Use_Hardware()
{
	const char *value = std::getenv("W3D_D3D12_TEST_HARDWARE");
	return value != nullptr && value[0] == '1';
}

bool Close_Enough(uint32_t a, uint32_t b)
{
	for (int shift = 0; shift < 32; shift += 8) {
		const int ca = int((a >> shift) & 0xff);
		const int cb = int((b >> shift) & 0xff);
		if (std::abs(ca - cb) > 2) {
			return false;
		}
	}
	return true;
}

struct SmokeVertex
{
	float		X, Y, Z;
	uint32_t	Color;		// D3DCOLOR, 0xAARRGGBB
};

class SmokePipeline
{
public:
	bool Create(ID3D12Device *device, DXGI_FORMAT rt_format, DXGI_FORMAT ds_format)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		param.Constants.ShaderRegister = 0;
		param.Constants.Num32BitValues = 4;
		param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
		rs_desc.NumParameters = 1;
		rs_desc.pParameters = &param;
		rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> blob, error;
		if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
			return false;
		}
		if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&RootSignature)))) {
			return false;
		}

		const D3D12_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = RootSignature.Get();
		desc.VS = { g_d3d12_smoke_vs, sizeof(g_d3d12_smoke_vs) };
		desc.PS = { g_d3d12_smoke_ps, sizeof(g_d3d12_smoke_ps) };
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.DepthStencilState.DepthEnable = TRUE;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.InputLayout = { elements, 2 };
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = rt_format;
		desc.DSVFormat = ds_format;
		desc.SampleDesc.Count = 1;
		return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&PipelineState)));
	}

	bool Draw(D3D12DeviceClass &device, float tint)
	{
		// Triangle covering the centre of the screen.
		const SmokeVertex vertices[3] = {
			{ -0.5f, -0.5f, 0.5f, 0xFFFF0000u },
			{  0.0f,  0.5f, 0.5f, 0xFFFF0000u },
			{  0.5f, -0.5f, 0.5f, 0xFFFF0000u },
		};

		D3D12UploadRingClass::Allocation vb;
		if (!device.Upload().Allocate(sizeof(vertices), 16, vb)) {
			return false;
		}
		memcpy(vb.CPU, vertices, sizeof(vertices));

		D3D12_VERTEX_BUFFER_VIEW view = {};
		view.BufferLocation = vb.GPU;
		view.SizeInBytes = sizeof(vertices);
		view.StrideInBytes = sizeof(SmokeVertex);

		const float constants[4] = { tint, tint, tint, 1.0f };
		ID3D12GraphicsCommandList *list = device.Command_List();
		list->SetGraphicsRootSignature(RootSignature.Get());
		list->SetPipelineState(PipelineState.Get());
		list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
		list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		list->IASetVertexBuffers(0, 1, &view);
		list->DrawInstanced(3, 1, 0, 0);
		return true;
	}

private:
	ComPtr<ID3D12RootSignature>	RootSignature;
	ComPtr<ID3D12PipelineState>	PipelineState;
};

bool Render_And_Check(D3D12DeviceClass &device, SmokePipeline &pipeline, float tint, uint32_t expected_center, const char *label)
{
	const float blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	device.Begin_Frame();
	device.Clear(true, true, blue, 1.0f, 0);
	bool ok = Expect(pipeline.Draw(device, tint), "upload ring allocation");

	std::vector<uint32_t> pixels;
	unsigned width = 0, height = 0;
	ok &= Expect(device.Read_Back_Buffer(pixels, width, height), "back buffer readback");
	ok &= Expect(device.End_Frame(false, 0), "frame submission");
	if (!ok) {
		return false;
	}

	ok &= Expect(width == device.Width() && height == device.Height(), "readback size matches the back buffer");
	const uint32_t center = pixels[size_t(height / 2) * width + width / 2];
	const uint32_t corner = pixels[0];
	if (!Close_Enough(center, expected_center) || corner != CLEAR_BLUE) {
		std::fprintf(stderr, "FAILED: %s: center 0x%08x (want 0x%08x), corner 0x%08x (want 0x%08x)\n",
			label, center, expected_center, corner, CLEAR_BLUE);
		return false;
	}
	return ok;
}

LRESULT CALLBACK Test_Window_Proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND Create_Test_Window(unsigned width, unsigned height)
{
	WNDCLASSW wc = {};
	wc.lpfnWndProc = Test_Window_Proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"OpenW3D_D3D12_Test";
	RegisterClassW(&wc);
	return CreateWindowExW(0, wc.lpszClassName, L"OpenW3D D3D12 test", WS_OVERLAPPEDWINDOW,
		0, 0, int(width), int(height), nullptr, nullptr, wc.hInstance, nullptr);
}

bool Test_Headless()
{
	D3D12DeviceInitParams params;
	params.Width = 128;
	params.Height = 96;
	params.UseWarp = !Use_Hardware();
	params.DebugLayer = true;

	D3D12DeviceClass device;
	if (!Expect(device.Init(params), "headless device init")) {
		return false;
	}

	SmokePipeline pipeline;
	bool ok = Expect(pipeline.Create(device.Device(), device.Back_Buffer_Format(), device.Depth_Format()), "pipeline creation");
	if (!ok) {
		return false;
	}

	ok &= Render_And_Check(device, pipeline, 1.0f, PURE_RED, "first frame");
	ok &= Render_And_Check(device, pipeline, 0.5f, HALF_RED, "root constants");

	// Cycle every frame slot several times so upload pages get recycled.
	for (int frame = 0; frame < 8 && ok; ++frame) {
		ok &= Render_And_Check(device, pipeline, 1.0f, PURE_RED, "frame slot cycling");
	}

	ok &= Expect(device.Resize(200, 150), "resize");
	ok &= Expect(device.Width() == 200 && device.Height() == 150, "resize updates the size");
	ok &= Render_And_Check(device, pipeline, 1.0f, PURE_RED, "after resize");
	return ok;
}

bool Test_Swap_Chain()
{
	HWND window = Create_Test_Window(160, 120);
	if (!Expect(window != nullptr, "test window")) {
		return false;
	}

	D3D12DeviceInitParams params;
	params.Window = window;
	params.Width = 160;
	params.Height = 120;
	params.UseWarp = !Use_Hardware();

	bool ok = true;
	{
		D3D12DeviceClass device;
		ok &= Expect(device.Init(params), "swap chain device init");
		if (ok) {
			const float blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			for (int frame = 0; frame < 6 && ok; ++frame) {
				device.Begin_Frame();
				device.Clear(true, true, blue, 1.0f, 0);
				ok &= Expect(device.End_Frame(true, 0), "present");
			}
			ok &= Expect(device.Resize(320, 240), "swap chain resize");
			device.Begin_Frame();
			device.Clear(true, true, blue, 1.0f, 0);
			ok &= Expect(device.End_Frame(true, 1), "present after resize");
		}
	}
	DestroyWindow(window);
	return ok;
}
}

int main()
{
	bool ok = true;
	ok &= Test_Headless();
	ok &= Test_Swap_Chain();
	if (ok) {
		std::cout << "D3D12 device tests passed\n";
	}
	return ok ? 0 : 1;
}

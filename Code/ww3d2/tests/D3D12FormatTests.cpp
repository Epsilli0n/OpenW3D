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

#include "d3d12formats.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int Failures = 0;

void Check(bool condition, const char *what)
{
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		++Failures;
	}
}

void Check_Pixel(uint32_t got, uint32_t want, const char *what)
{
	if (got != want) {
		std::fprintf(stderr, "FAILED: %s: got 0x%08x, want 0x%08x\n", what, got, want);
		++Failures;
	}
}

uint32_t Decode_One(D3DFORMAT format, const void *data)
{
	uint32_t pixel = 0;
	D3D12_Decode(format, data, 16, 1, 1, &pixel, 1);
	return pixel;
}

void Test_Layouts()
{
	Check(D3D12_Row_Pitch(D3DFMT_A8R8G8B8, 10) == 40, "A8R8G8B8 pitch");
	Check(D3D12_Row_Pitch(D3DFMT_R8G8B8, 10) == 30, "R8G8B8 pitch");
	Check(D3D12_Row_Pitch(D3DFMT_R5G6B5, 10) == 20, "R5G6B5 pitch");
	Check(D3D12_Row_Pitch(D3DFMT_DXT1, 16) == 32, "DXT1 pitch is per block row");
	Check(D3D12_Row_Pitch(D3DFMT_DXT5, 2) == 16, "DXT5 partial block pitch");
	Check(D3D12_Row_Count(D3DFMT_DXT3, 6) == 2, "DXT row count rounds up");
	Check(D3D12_Level_Size(D3DFMT_DXT1, 1, 1) == 8, "DXT1 1x1 level is one block");
	Check(!D3D12_Format_Layout(D3DFMT_P8).Supported, "P8 is unsupported");
}

void Test_Uncompressed()
{
	const uint8_t argb[4] = { 0x30, 0x20, 0x10, 0x80 };	// B, G, R, A in memory
	Check_Pixel(Decode_One(D3DFMT_A8R8G8B8, argb), 0x80102030u, "A8R8G8B8 decode");
	Check_Pixel(Decode_One(D3DFMT_X8R8G8B8, argb), 0xFF102030u, "X8R8G8B8 forces alpha");

	const uint16_t red565 = 0xF800;
	Check_Pixel(Decode_One(D3DFMT_R5G6B5, &red565), 0xFFFF0000u, "R5G6B5 red");
	const uint16_t green565 = 0x07E0;
	Check_Pixel(Decode_One(D3DFMT_R5G6B5, &green565), 0xFF00FF00u, "R5G6B5 green");

	const uint16_t half4444 = 0x8F00;	// a=8, r=f
	Check_Pixel(Decode_One(D3DFMT_A4R4G4B4, &half4444), 0x88FF0000u, "A4R4G4B4 decode");

	const uint16_t transparent1555 = 0x7C00;
	Check_Pixel(Decode_One(D3DFMT_A1R5G5B5, &transparent1555), 0x00FF0000u, "A1R5G5B5 alpha bit clear");
	Check_Pixel(Decode_One(D3DFMT_X1R5G5B5, &transparent1555), 0xFFFF0000u, "X1R5G5B5 ignores alpha bit");

	const uint8_t a8 = 0x40;
	Check_Pixel(Decode_One(D3DFMT_A8, &a8), 0x40000000u, "A8 colour reads as 0");
	const uint8_t l8 = 0x7F;
	Check_Pixel(Decode_One(D3DFMT_L8, &l8), 0xFF7F7F7Fu, "L8 replicates and alpha is 1");
	const uint8_t a8l8[2] = { 0x11, 0x99 };
	Check_Pixel(Decode_One(D3DFMT_A8L8, a8l8), 0x99111111u, "A8L8 decode");

	// Round trips through every encodable format must be stable after the first pass.
	const D3DFORMAT formats[] = {
		D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5,
		D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4, D3DFMT_X4R4G4B4, D3DFMT_R3G3B2, D3DFMT_A8R3G3B2,
		D3DFMT_A8, D3DFMT_L8, D3DFMT_A8L8, D3DFMT_A4L4,
	};
	uint32_t source[64];
	for (unsigned i = 0; i < 64; ++i) {
		source[i] = 0x01234567u * (i + 1) ^ (i << 13);
	}
	for (D3DFORMAT format : formats) {
		std::vector<uint8_t> encoded(64 * 4);
		uint32_t once[64], twice[64];
		Check(D3D12_Encode(format, source, 8, 8, 8, encoded.data(), D3D12_Row_Pitch(format, 8)), "encode");
		D3D12_Decode(format, encoded.data(), D3D12_Row_Pitch(format, 8), 8, 8, once, 8);
		Check(D3D12_Encode(format, once, 8, 8, 8, encoded.data(), D3D12_Row_Pitch(format, 8)), "re-encode");
		D3D12_Decode(format, encoded.data(), D3D12_Row_Pitch(format, 8), 8, 8, twice, 8);
		if (memcmp(once, twice, sizeof(once)) != 0) {
			std::fprintf(stderr, "FAILED: round trip is unstable for format %d\n", int(format));
			++Failures;
		}
	}
}

void Test_DXT()
{
	// DXT1 block: color0 = red, color1 = blue, indices select 0,1,2,3 across the first row.
	const uint8_t dxt1[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00 };
	uint32_t pixels[16];
	D3D12_Decode(D3DFMT_DXT1, dxt1, 8, 4, 4, pixels, 4);
	Check_Pixel(pixels[0], 0xFFFF0000u, "DXT1 colour 0");
	Check_Pixel(pixels[1], 0xFF0000FFu, "DXT1 colour 1");
	Check_Pixel(pixels[2], 0xFFAA0055u, "DXT1 2/3 interpolation");
	Check_Pixel(pixels[3], 0xFF5500AAu, "DXT1 1/3 interpolation");
	Check_Pixel(pixels[4], 0xFFFF0000u, "DXT1 second row uses index 0");

	// Same colours with color0 <= color1 enable the transparent entry.
	const uint8_t dxt1a[8] = { 0x1F, 0x00, 0x00, 0xF8, 0x0B, 0x00, 0x00, 0x00 };	// indices 3, 2, 0, ...
	D3D12_Decode(D3DFMT_DXT1, dxt1a, 8, 4, 4, pixels, 4);
	Check_Pixel(pixels[0], 0x00000000u, "DXT1 transparent index");
	Check_Pixel(pixels[1], 0xFF7F007Fu, "DXT1 3-colour midpoint");

	// DXT5: alpha endpoints 255 and 0, first texel index 1 (alpha 0), rest index 0 (255).
	uint8_t dxt5[16] = { 0xFF, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
	memcpy(dxt5 + 8, dxt1, 8);
	D3D12_Decode(D3DFMT_DXT5, dxt5, 16, 4, 4, pixels, 4);
	Check_Pixel(pixels[0], 0x00FF0000u, "DXT5 alpha index 1");
	Check_Pixel(pixels[5], 0xFFFF0000u, "DXT5 alpha index 0");

	// DXT3: explicit 4-bit alpha.
	uint8_t dxt3[16] = { 0x5A };
	memcpy(dxt3 + 8, dxt1, 8);
	D3D12_Decode(D3DFMT_DXT3, dxt3, 16, 4, 4, pixels, 4);
	Check_Pixel(pixels[0], 0xAAFF0000u, "DXT3 low nibble alpha");
	Check_Pixel(pixels[1], 0x550000FFu, "DXT3 high nibble alpha");

	// Partial blocks clip to the requested size.
	uint32_t small[4] = { 1, 1, 1, 1 };
	D3D12_Decode(D3DFMT_DXT1, dxt1, 8, 2, 2, small, 2);
	Check_Pixel(small[1], 0xFF0000FFu, "DXT1 2x2 clip");
}

void Test_Upload_Formats()
{
	Check(D3D12_Upload_Format(D3DFMT_DXT1, 64, 64) == DXGI_FORMAT_BC1_UNORM, "DXT1 uploads as BC1");
	Check(D3D12_Upload_Format(D3DFMT_DXT5, 2, 2) == DXGI_FORMAT_B8G8R8A8_UNORM, "small DXT5 is decoded");
	Check(D3D12_Upload_Format(D3DFMT_R5G6B5, 64, 64) == DXGI_FORMAT_B8G8R8A8_UNORM, "16-bit uploads as BGRA8");

	const uint16_t pixels[2] = { 0xF800, 0x001F };
	uint32_t out[2] = {};
	D3D12_Convert_For_Upload(D3DFMT_R5G6B5, DXGI_FORMAT_B8G8R8A8_UNORM, pixels, 4, 2, 1, out, 8);
	Check_Pixel(out[0], 0xFFFF0000u, "upload conversion red");
	Check_Pixel(out[1], 0xFF0000FFu, "upload conversion blue");
}

void Test_Scale()
{
	const uint32_t src[4] = { 0xFF000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu };
	uint32_t dst = 0;
	D3D12_Scale(src, 2, 2, 2, &dst, 1, 1, 1, false);
	Check_Pixel(dst, 0xFF808080u, "box filter 2x2 average");

	D3D12_Scale(src, 2, 2, 2, &dst, 1, 1, 1, true);
	Check_Pixel(dst, 0xFF000000u, "point filter picks the first texel");

	uint32_t up[16] = {};
	D3D12_Scale(src, 2, 2, 2, up, 4, 4, 4, true);
	Check_Pixel(up[3], 0xFFFFFFFFu, "point upscale");
}
}

int main()
{
	Test_Layouts();
	Test_Uncompressed();
	Test_DXT();
	Test_Upload_Formats();
	Test_Scale();
	if (Failures == 0) {
		std::puts("D3D12 format tests passed");
	}
	return Failures == 0 ? 0 : 1;
}

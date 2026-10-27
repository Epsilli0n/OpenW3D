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

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
	inline uint32_t Pack(unsigned a, unsigned r, unsigned g, unsigned b)
	{
		return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
	}

	// Expand an n-bit channel to 8 bits by bit replication.
	inline unsigned Expand(unsigned value, unsigned bits)
	{
		switch (bits) {
		case 1: return value ? 255 : 0;
		case 2: return (value << 6) | (value << 4) | (value << 2) | value;
		case 3: return (value << 5) | (value << 2) | (value >> 1);
		case 4: return (value << 4) | value;
		case 5: return (value << 3) | (value >> 2);
		case 6: return (value << 2) | (value >> 4);
		default: return value;
		}
	}

	// Reduce an 8-bit channel to n bits with rounding.
	inline unsigned Reduce(unsigned value, unsigned bits)
	{
		const unsigned max = (1u << bits) - 1;
		return (value * max + 127) / 255;
	}

	inline unsigned A(uint32_t p) { return p >> 24; }
	inline unsigned R(uint32_t p) { return (p >> 16) & 0xff; }
	inline unsigned G(uint32_t p) { return (p >> 8) & 0xff; }
	inline unsigned B(uint32_t p) { return p & 0xff; }

	inline unsigned Luminance(uint32_t p)
	{
		// Rec. 601 weights, as D3DX uses for luminance conversion.
		return (R(p) * 299 + G(p) * 587 + B(p) * 114 + 500) / 1000;
	}

	uint32_t Decode_565(uint16_t c)
	{
		return Pack(255, Expand((c >> 11) & 0x1f, 5), Expand((c >> 5) & 0x3f, 6), Expand(c & 0x1f, 5));
	}

	// Decode one 4x4 DXT colour block into 16 pixels. 'dxt1' enables the 1-bit alpha mode.
	void Decode_Color_Block(const uint8_t *block, uint32_t out[16], bool dxt1)
	{
		const uint16_t c0 = uint16_t(block[0] | (block[1] << 8));
		const uint16_t c1 = uint16_t(block[2] | (block[3] << 8));
		uint32_t colors[4];
		colors[0] = Decode_565(c0);
		colors[1] = Decode_565(c1);
		if (c0 > c1 || !dxt1) {
			colors[2] = Pack(255, (2 * R(colors[0]) + R(colors[1]) + 1) / 3, (2 * G(colors[0]) + G(colors[1]) + 1) / 3, (2 * B(colors[0]) + B(colors[1]) + 1) / 3);
			colors[3] = Pack(255, (R(colors[0]) + 2 * R(colors[1]) + 1) / 3, (G(colors[0]) + 2 * G(colors[1]) + 1) / 3, (B(colors[0]) + 2 * B(colors[1]) + 1) / 3);
		} else {
			colors[2] = Pack(255, (R(colors[0]) + R(colors[1])) / 2, (G(colors[0]) + G(colors[1])) / 2, (B(colors[0]) + B(colors[1])) / 2);
			colors[3] = 0;
		}
		const uint32_t bits = uint32_t(block[4]) | (uint32_t(block[5]) << 8) | (uint32_t(block[6]) << 16) | (uint32_t(block[7]) << 24);
		for (int i = 0; i < 16; ++i) {
			out[i] = colors[(bits >> (i * 2)) & 3];
		}
	}

	void Decode_Block(D3DFORMAT format, const uint8_t *block, uint32_t out[16])
	{
		switch (format) {
		case D3DFMT_DXT1:
			Decode_Color_Block(block, out, true);
			break;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
			Decode_Color_Block(block + 8, out, false);
			for (int i = 0; i < 16; ++i) {
				const unsigned alpha = (block[i / 2] >> ((i & 1) * 4)) & 0xf;
				out[i] = (out[i] & 0x00ffffff) | (uint32_t(Expand(alpha, 4)) << 24);
			}
			break;
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
		{
			Decode_Color_Block(block + 8, out, false);
			unsigned alphas[8];
			alphas[0] = block[0];
			alphas[1] = block[1];
			if (alphas[0] > alphas[1]) {
				for (int i = 1; i < 7; ++i) {
					alphas[i + 1] = ((7 - i) * alphas[0] + i * alphas[1] + 3) / 7;
				}
			} else {
				for (int i = 1; i < 5; ++i) {
					alphas[i + 1] = ((5 - i) * alphas[0] + i * alphas[1] + 2) / 5;
				}
				alphas[6] = 0;
				alphas[7] = 255;
			}
			uint64_t bits = 0;
			for (int i = 0; i < 6; ++i) {
				bits |= uint64_t(block[2 + i]) << (8 * i);
			}
			for (int i = 0; i < 16; ++i) {
				out[i] = (out[i] & 0x00ffffff) | (uint32_t(alphas[(bits >> (3 * i)) & 7]) << 24);
			}
			break;
		}
		default:
			std::fill(out, out + 16, 0u);
			break;
		}
	}

	uint32_t Decode_Pixel(D3DFORMAT format, const uint8_t *p)
	{
		switch (format) {
		case D3DFMT_A8R8G8B8:	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
		case D3DFMT_X8R8G8B8:	return Pack(255, p[2], p[1], p[0]);
		case D3DFMT_R8G8B8:		return Pack(255, p[2], p[1], p[0]);
		case D3DFMT_R5G6B5:		return Decode_565(uint16_t(p[0] | (p[1] << 8)));
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5: {
			const unsigned c = p[0] | (p[1] << 8);
			const unsigned a = (format == D3DFMT_A1R5G5B5) ? Expand(c >> 15, 1) : 255;
			return Pack(a, Expand((c >> 10) & 0x1f, 5), Expand((c >> 5) & 0x1f, 5), Expand(c & 0x1f, 5));
		}
		case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4: {
			const unsigned c = p[0] | (p[1] << 8);
			const unsigned a = (format == D3DFMT_A4R4G4B4) ? Expand(c >> 12, 4) : 255;
			return Pack(a, Expand((c >> 8) & 0xf, 4), Expand((c >> 4) & 0xf, 4), Expand(c & 0xf, 4));
		}
		case D3DFMT_R3G3B2:		return Pack(255, Expand(p[0] >> 5, 3), Expand((p[0] >> 2) & 7, 3), Expand(p[0] & 3, 2));
		case D3DFMT_A8R3G3B2:	return Pack(p[1], Expand(p[0] >> 5, 3), Expand((p[0] >> 2) & 7, 3), Expand(p[0] & 3, 2));
		case D3DFMT_A8:			return Pack(p[0], 0, 0, 0);
		case D3DFMT_L8:			return Pack(255, p[0], p[0], p[0]);
		case D3DFMT_A8L8:			return Pack(p[1], p[0], p[0], p[0]);
		case D3DFMT_A4L4: {
			const unsigned l = Expand(p[0] & 0xf, 4);
			return Pack(Expand(p[0] >> 4, 4), l, l, l);
		}
		// Bump formats: stored as raw bytes (u in blue, v in green, luminance in red).
		case D3DFMT_V8U8:			return Pack(255, 255, p[1], p[0]);
		case D3DFMT_X8L8V8U8:	return Pack(255, p[2], p[1], p[0]);
		case D3DFMT_L6V5U5: {
			const unsigned c = p[0] | (p[1] << 8);
			return Pack(255, Expand(c >> 10, 6), (c >> 5) & 0x1f, c & 0x1f);
		}
		default:					return 0;
		}
	}

	void Encode_Pixel(D3DFORMAT format, uint32_t c, uint8_t *p)
	{
		switch (format) {
		case D3DFMT_A8R8G8B8:	p[0] = uint8_t(B(c)); p[1] = uint8_t(G(c)); p[2] = uint8_t(R(c)); p[3] = uint8_t(A(c)); break;
		case D3DFMT_X8R8G8B8:	p[0] = uint8_t(B(c)); p[1] = uint8_t(G(c)); p[2] = uint8_t(R(c)); p[3] = 0xff; break;
		case D3DFMT_R8G8B8:		p[0] = uint8_t(B(c)); p[1] = uint8_t(G(c)); p[2] = uint8_t(R(c)); break;
		case D3DFMT_R5G6B5: {
			const unsigned v = (Reduce(R(c), 5) << 11) | (Reduce(G(c), 6) << 5) | Reduce(B(c), 5);
			p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
			break;
		}
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5: {
			const unsigned a = (format == D3DFMT_A1R5G5B5) ? (A(c) >= 128 ? 1u : 0u) : 1u;
			const unsigned v = (a << 15) | (Reduce(R(c), 5) << 10) | (Reduce(G(c), 5) << 5) | Reduce(B(c), 5);
			p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
			break;
		}
		case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4: {
			const unsigned a = (format == D3DFMT_A4R4G4B4) ? Reduce(A(c), 4) : 0xf;
			const unsigned v = (a << 12) | (Reduce(R(c), 4) << 8) | (Reduce(G(c), 4) << 4) | Reduce(B(c), 4);
			p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
			break;
		}
		case D3DFMT_R3G3B2:		p[0] = uint8_t((Reduce(R(c), 3) << 5) | (Reduce(G(c), 3) << 2) | Reduce(B(c), 2)); break;
		case D3DFMT_A8R3G3B2:	p[0] = uint8_t((Reduce(R(c), 3) << 5) | (Reduce(G(c), 3) << 2) | Reduce(B(c), 2)); p[1] = uint8_t(A(c)); break;
		case D3DFMT_A8:			p[0] = uint8_t(A(c)); break;
		case D3DFMT_L8:			p[0] = uint8_t(Luminance(c)); break;
		case D3DFMT_A8L8:			p[0] = uint8_t(Luminance(c)); p[1] = uint8_t(A(c)); break;
		case D3DFMT_A4L4:			p[0] = uint8_t((Reduce(A(c), 4) << 4) | Reduce(Luminance(c), 4)); break;
		case D3DFMT_V8U8:			p[0] = uint8_t(B(c)); p[1] = uint8_t(G(c)); break;
		case D3DFMT_X8L8V8U8:	p[0] = uint8_t(B(c)); p[1] = uint8_t(G(c)); p[2] = uint8_t(R(c)); p[3] = 0; break;
		case D3DFMT_L6V5U5: {
			const unsigned v = (Reduce(R(c), 6) << 10) | ((G(c) & 0x1f) << 5) | (B(c) & 0x1f);
			p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
			break;
		}
		default:
			break;
		}
	}
}

D3D12FormatLayout D3D12_Format_Layout(D3DFORMAT format)
{
	D3D12FormatLayout layout;
	layout.Supported = true;
	switch (format) {
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
	case D3DFMT_X8L8V8U8:	layout.BytesPerElement = 4; break;
	case D3DFMT_R8G8B8:		layout.BytesPerElement = 3; break;
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_A8R3G3B2:
	case D3DFMT_A8L8:
	case D3DFMT_V8U8:
	case D3DFMT_L6V5U5:		layout.BytesPerElement = 2; break;
	case D3DFMT_R3G3B2:
	case D3DFMT_A8:
	case D3DFMT_L8:
	case D3DFMT_A4L4:			layout.BytesPerElement = 1; break;
	case D3DFMT_DXT1:			layout.BytesPerElement = 8; layout.Compressed = true; break;
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:			layout.BytesPerElement = 16; layout.Compressed = true; break;
	default:						layout.Supported = false; break;
	}
	return layout;
}

unsigned D3D12_Row_Pitch(D3DFORMAT format, unsigned width)
{
	const D3D12FormatLayout layout = D3D12_Format_Layout(format);
	const unsigned elements = layout.Compressed ? std::max(1u, (width + 3) / 4) : width;
	return elements * layout.BytesPerElement;
}

unsigned D3D12_Row_Count(D3DFORMAT format, unsigned height)
{
	return D3D12_Format_Layout(format).Compressed ? std::max(1u, (height + 3) / 4) : height;
}

unsigned D3D12_Level_Size(D3DFORMAT format, unsigned width, unsigned height)
{
	return D3D12_Row_Pitch(format, width) * D3D12_Row_Count(format, height);
}

DXGI_FORMAT D3D12_Upload_Format(D3DFORMAT format, unsigned width, unsigned height)
{
	// D3D12 requires block-compressed textures to have a top level that is a whole number of blocks.
	const bool blocks_fit = (width % 4) == 0 && (height % 4) == 0;
	switch (format) {
	case D3DFMT_DXT1:	return blocks_fit ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:	return blocks_fit ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:	return blocks_fit ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_V8U8:
	case D3DFMT_L6V5U5:
	case D3DFMT_X8L8V8U8:	return DXGI_FORMAT_R8G8B8A8_SNORM;
	default:				return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}

bool D3D12_Is_Render_Target_Format(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_R8G8B8:
		return true;
	default:
		return false;
	}
}

bool D3D12_Is_Depth_Format(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_D16:
	case D3DFMT_D15S1:
	case D3DFMT_D24X8:
	case D3DFMT_D24S8:
	case D3DFMT_D24X4S4:
	case D3DFMT_D32:
		return true;
	default:
		return false;
	}
}

void D3D12_Decode(D3DFORMAT format, const void *src, unsigned src_pitch,
	unsigned width, unsigned height, uint32_t *pixels, unsigned pixel_pitch)
{
	const D3D12FormatLayout layout = D3D12_Format_Layout(format);
	const uint8_t *rows = static_cast<const uint8_t *>(src);

	if (layout.Compressed) {
		uint32_t block[16];
		for (unsigned by = 0; by < (height + 3) / 4; ++by) {
			const uint8_t *block_row = rows + size_t(by) * src_pitch;
			for (unsigned bx = 0; bx < (width + 3) / 4; ++bx) {
				Decode_Block(format, block_row + size_t(bx) * layout.BytesPerElement, block);
				for (unsigned py = 0; py < 4; ++py) {
					const unsigned y = by * 4 + py;
					if (y >= height) break;
					for (unsigned px = 0; px < 4; ++px) {
						const unsigned x = bx * 4 + px;
						if (x >= width) break;
						pixels[size_t(y) * pixel_pitch + x] = block[py * 4 + px];
					}
				}
			}
		}
		return;
	}

	for (unsigned y = 0; y < height; ++y) {
		const uint8_t *row = rows + size_t(y) * src_pitch;
		uint32_t *out = pixels + size_t(y) * pixel_pitch;
		for (unsigned x = 0; x < width; ++x) {
			out[x] = Decode_Pixel(format, row + size_t(x) * layout.BytesPerElement);
		}
	}
}

bool D3D12_Encode(D3DFORMAT format, const uint32_t *pixels, unsigned pixel_pitch,
	unsigned width, unsigned height, void *dst, unsigned dst_pitch)
{
	const D3D12FormatLayout layout = D3D12_Format_Layout(format);
	if (!layout.Supported || layout.Compressed) {
		return false;
	}
	uint8_t *rows = static_cast<uint8_t *>(dst);
	for (unsigned y = 0; y < height; ++y) {
		uint8_t *row = rows + size_t(y) * dst_pitch;
		const uint32_t *in = pixels + size_t(y) * pixel_pitch;
		for (unsigned x = 0; x < width; ++x) {
			Encode_Pixel(format, in[x], row + size_t(x) * layout.BytesPerElement);
		}
	}
	return true;
}

void D3D12_Convert_For_Upload(D3DFORMAT format, DXGI_FORMAT gpu_format, const void *src, unsigned src_pitch,
	unsigned width, unsigned height, void *dst, unsigned dst_pitch)
{
	const uint8_t *src_rows = static_cast<const uint8_t *>(src);
	uint8_t *dst_rows = static_cast<uint8_t *>(dst);

	switch (gpu_format) {
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC3_UNORM: {
		// Same block layout; copy block rows.
		const unsigned row_bytes = D3D12_Row_Pitch(format, width);
		const unsigned rows = D3D12_Row_Count(format, height);
		for (unsigned y = 0; y < rows; ++y) {
			memcpy(dst_rows + size_t(y) * dst_pitch, src_rows + size_t(y) * src_pitch, row_bytes);
		}
		return;
	}
	case DXGI_FORMAT_R8G8B8A8_SNORM: {
		// Bump maps: signed du/dv, unsigned luminance halved into the positive range.
		const unsigned bpp = D3D12_Format_Layout(format).BytesPerElement;
		for (unsigned y = 0; y < height; ++y) {
			const uint8_t *row = src_rows + size_t(y) * src_pitch;
			int8_t *out = reinterpret_cast<int8_t *>(dst_rows + size_t(y) * dst_pitch);
			for (unsigned x = 0; x < width; ++x) {
				const uint32_t raw = Decode_Pixel(format, row + size_t(x) * bpp);
				int du = int8_t(B(raw));
				int dv = int8_t(G(raw));
				if (format == D3DFMT_L6V5U5) {
					// 5-bit signed components.
					du = (B(raw) & 0x10) ? int(B(raw)) - 32 : int(B(raw));
					dv = (G(raw) & 0x10) ? int(G(raw)) - 32 : int(G(raw));
					du = du * 127 / 15;
					dv = dv * 127 / 15;
				}
				out[x * 4 + 0] = int8_t(std::max(-127, du));
				out[x * 4 + 1] = int8_t(std::max(-127, dv));
				out[x * 4 + 2] = int8_t(R(raw) / 2);
				out[x * 4 + 3] = 127;
			}
		}
		return;
	}
	default: {
		// B8G8R8A8: 0xAARRGGBB as little-endian uint32 is exactly the pixel encoding.
		if (format == D3DFMT_A8R8G8B8) {
			for (unsigned y = 0; y < height; ++y) {
				memcpy(dst_rows + size_t(y) * dst_pitch, src_rows + size_t(y) * src_pitch, size_t(width) * 4);
			}
			return;
		}
		if (D3D12_Format_Layout(format).Compressed) {
			// Odd-sized compressed level: decode straight into the destination.
			D3D12_Decode(format, src, src_pitch, width, height, reinterpret_cast<uint32_t *>(dst), dst_pitch / 4);
			return;
		}
		for (unsigned y = 0; y < height; ++y) {
			D3D12_Decode(format, src_rows + size_t(y) * src_pitch, src_pitch, width, 1,
				reinterpret_cast<uint32_t *>(dst_rows + size_t(y) * dst_pitch), width);
		}
		return;
	}
	}
}

void D3D12_Scale(const uint32_t *src, unsigned src_pitch, unsigned src_width, unsigned src_height,
	uint32_t *dst, unsigned dst_pitch, unsigned dst_width, unsigned dst_height, bool point)
{
	if (src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) {
		return;
	}

	if (point || (src_width == dst_width && src_height == dst_height)) {
		for (unsigned y = 0; y < dst_height; ++y) {
			const unsigned sy = unsigned((uint64_t(y) * src_height) / dst_height);
			for (unsigned x = 0; x < dst_width; ++x) {
				const unsigned sx = unsigned((uint64_t(x) * src_width) / dst_width);
				dst[size_t(y) * dst_pitch + x] = src[size_t(sy) * src_pitch + sx];
			}
		}
		return;
	}

	// Box filter: average every source pixel that overlaps the destination pixel. For
	// exact 2:1 reductions (mip generation) this is the classic 2x2 average.
	for (unsigned y = 0; y < dst_height; ++y) {
		unsigned y0 = unsigned((uint64_t(y) * src_height) / dst_height);
		unsigned y1 = unsigned((uint64_t(y + 1) * src_height + dst_height - 1) / dst_height);
		y1 = std::max(y1, y0 + 1);
		for (unsigned x = 0; x < dst_width; ++x) {
			unsigned x0 = unsigned((uint64_t(x) * src_width) / dst_width);
			unsigned x1 = unsigned((uint64_t(x + 1) * src_width + dst_width - 1) / dst_width);
			x1 = std::max(x1, x0 + 1);
			unsigned sum[4] = { 0, 0, 0, 0 };
			unsigned count = 0;
			for (unsigned sy = y0; sy < y1 && sy < src_height; ++sy) {
				for (unsigned sx = x0; sx < x1 && sx < src_width; ++sx) {
					const uint32_t p = src[size_t(sy) * src_pitch + sx];
					sum[0] += A(p); sum[1] += R(p); sum[2] += G(p); sum[3] += B(p);
					++count;
				}
			}
			const unsigned half = count / 2;
			dst[size_t(y) * dst_pitch + x] = Pack((sum[0] + half) / count, (sum[1] + half) / count,
				(sum[2] + half) / count, (sum[3] + half) / count);
		}
	}
}

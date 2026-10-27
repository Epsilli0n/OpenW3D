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
** Pixel formats for the D3D12 backend.
**
** The engine keeps working in its D3D9-era formats (A4R4G4B4 font pages, R5G6B5 and
** DXT textures, ...). CPU copies of resources stay in those formats so Lock() behaves
** as before; uploads convert them to a format D3D12 can sample. Uncompressed formats
** become B8G8R8A8_UNORM with D3D9 semantics for missing channels (1, except A8 whose
** colour is 0). DXT formats upload as the equivalent BC format when the size allows.
*/

#pragma once

#ifndef D3D12FORMATS_H
#define D3D12FORMATS_H

#include <windows.h>
#include <d3d9types.h>
#include <dxgiformat.h>

#include <cstdint>

struct D3D12FormatLayout
{
	bool			Supported = false;
	bool			Compressed = false;		// 4x4 blocks
	unsigned		BytesPerElement = 0;		// bytes per pixel, or per 4x4 block when compressed
};

D3D12FormatLayout	D3D12_Format_Layout(D3DFORMAT format);

// Bytes per row of pixels (per row of blocks when compressed).
unsigned				D3D12_Row_Pitch(D3DFORMAT format, unsigned width);
// Rows of pixels (rows of blocks when compressed).
unsigned				D3D12_Row_Count(D3DFORMAT format, unsigned height);
// Total bytes for one tightly packed level.
unsigned				D3D12_Level_Size(D3DFORMAT format, unsigned width, unsigned height);

// GPU format used for a texture of this format and size.
DXGI_FORMAT			D3D12_Upload_Format(D3DFORMAT format, unsigned width, unsigned height);
// Format the engine sees for a render target of this format.
bool					D3D12_Is_Render_Target_Format(D3DFORMAT format);
bool					D3D12_Is_Depth_Format(D3DFORMAT format);

/*
** Conversion. 'pixels' are 0xAARRGGBB (D3DCOLOR / B8G8R8A8 in memory).
*/

// Decode a rectangle of an image into 32-bit pixels. For compressed formats x/y/width/height
// must be in pixels; partial blocks are clipped.
void					D3D12_Decode(D3DFORMAT format, const void *src, unsigned src_pitch,
										unsigned width, unsigned height, uint32_t *pixels, unsigned pixel_pitch);

// Encode 32-bit pixels into an uncompressed format. Returns false for compressed formats.
bool					D3D12_Encode(D3DFORMAT format, const uint32_t *pixels, unsigned pixel_pitch,
										unsigned width, unsigned height, void *dst, unsigned dst_pitch);

// Copy one level's CPU data into upload memory laid out for 'gpu_format'.
void					D3D12_Convert_For_Upload(D3DFORMAT format, DXGI_FORMAT gpu_format, const void *src, unsigned src_pitch,
										unsigned width, unsigned height, void *dst, unsigned dst_pitch);

// Scale 32-bit pixels with point sampling (filter == point/none) or a box/bilinear filter.
void					D3D12_Scale(const uint32_t *src, unsigned src_pitch, unsigned src_width, unsigned src_height,
										uint32_t *dst, unsigned dst_pitch, unsigned dst_width, unsigned dst_height, bool point);

#endif

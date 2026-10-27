/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#include "dx8fvf.h"
#include "wwstring.h"

// Size in bytes of one vertex described by an FVF code (same result as D3DXGetFVFVertexSize).
static unsigned Get_FVF_Vertex_Size(unsigned FVF)
{
	unsigned size = 0;

	switch (FVF & D3DFVF_POSITION_MASK) {
	case D3DFVF_XYZ:		size += 3 * sizeof(float); break;
	case D3DFVF_XYZRHW:	size += 4 * sizeof(float); break;
	case D3DFVF_XYZW:		size += 4 * sizeof(float); break;
	case D3DFVF_XYZB1:	size += 4 * sizeof(float); break;
	case D3DFVF_XYZB2:	size += 5 * sizeof(float); break;
	case D3DFVF_XYZB3:	size += 6 * sizeof(float); break;
	case D3DFVF_XYZB4:	size += 7 * sizeof(float); break;
	case D3DFVF_XYZB5:	size += 8 * sizeof(float); break;
	default:					break;
	}

	if (FVF & D3DFVF_NORMAL)	size += 3 * sizeof(float);
	if (FVF & D3DFVF_PSIZE)		size += sizeof(float);
	if (FVF & D3DFVF_DIFFUSE)	size += sizeof(uint32_t);
	if (FVF & D3DFVF_SPECULAR)	size += sizeof(uint32_t);

	const unsigned tex_count = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (unsigned i = 0; i < tex_count; ++i) {
		// Two format bits per coordinate set, starting at bit 16.
		switch ((FVF >> (16 + i * 2)) & 0x3) {
		case D3DFVF_TEXTUREFORMAT1:	size += 1 * sizeof(float); break;
		case D3DFVF_TEXTUREFORMAT2:	size += 2 * sizeof(float); break;
		case D3DFVF_TEXTUREFORMAT3:	size += 3 * sizeof(float); break;
		case D3DFVF_TEXTUREFORMAT4:	size += 4 * sizeof(float); break;
		}
	}
	return size;
}

FVFInfoClass::FVFInfoClass(unsigned FVF_)
	:
	FVF(FVF_),
	fvf_size(Get_FVF_Vertex_Size(FVF))
{
	location_offset=0;
	blend_offset=location_offset;

	if ((FVF&D3DFVF_XYZ)==D3DFVF_XYZ) blend_offset+=3*sizeof(float);
	normal_offset=blend_offset;

	if ( ((FVF&D3DFVF_XYZB4)==D3DFVF_XYZB4) &&
		  ((FVF&D3DFVF_LASTBETA_UBYTE4)==D3DFVF_LASTBETA_UBYTE4) ) normal_offset+=3*sizeof(float)+sizeof(uint32_t);
	diffuse_offset=normal_offset;

	if ((FVF&D3DFVF_NORMAL)==D3DFVF_NORMAL) diffuse_offset+=3*sizeof(float);
	specular_offset=diffuse_offset;

	if ((FVF&D3DFVF_DIFFUSE)==D3DFVF_DIFFUSE) specular_offset+=sizeof(unsigned);
	texcoord_offset[0]=specular_offset;

	if ((FVF&D3DFVF_SPECULAR)==D3DFVF_SPECULAR) texcoord_offset[0]+=sizeof(unsigned);

	for (unsigned int i=1; i<D3DDP_MAXTEXCOORD; i++)
	{
		texcoord_offset[i]=texcoord_offset[i-1];

		if ((int(FVF)&D3DFVF_TEXCOORDSIZE1(i-1))==D3DFVF_TEXCOORDSIZE1(i-1)) texcoord_offset[i]+=sizeof(float);
		else if ((int(FVF)&D3DFVF_TEXCOORDSIZE2(i-1))==D3DFVF_TEXCOORDSIZE2(i-1)) texcoord_offset[i]+=2*sizeof(float);
		else if ((int(FVF)&D3DFVF_TEXCOORDSIZE3(i-1))==D3DFVF_TEXCOORDSIZE3(i-1)) texcoord_offset[i]+=3*sizeof(float);
		else if ((int(FVF)&D3DFVF_TEXCOORDSIZE4(i-1))==D3DFVF_TEXCOORDSIZE4(i-1)) texcoord_offset[i]+=4*sizeof(float);
	}
}

void FVFInfoClass::Get_FVF_Name(StringClass& fvfname) const
{
	switch (Get_FVF()) {
	case DX8_FVF_XYZ: fvfname="D3DFVF_XYZ"; break;
	case DX8_FVF_XYZN: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL"; break;
	case DX8_FVF_XYZNUV1: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1"; break;
	case DX8_FVF_XYZNUV2: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2"; break;
	case DX8_FVF_XYZNDUV1: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZNDUV2: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZDUV1: fvfname="D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZDUV2: fvfname="D3DFVF_XYZ|D3DFVF_TEX2|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZUV1: fvfname="D3DFVF_XYZ|D3DFVF_TEX1"; break;
	case DX8_FVF_XYZUV2: fvfname="D3DFVF_XYZ|D3DFVF_TEX2"; break;
	default: fvfname="Unknown!";
	}
}

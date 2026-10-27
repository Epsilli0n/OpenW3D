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

// Checks the engine's replacements for D3DX helpers against D3DX itself, so the
// D3DX dependency can be dropped without changing results.

#include "dx8fvf.h"
#include "matrix3d.h"
#include "matrix4.h"

#include <d3dx9.h>

#include <cmath>
#include <cstdio>

namespace
{
bool Test_FVF_Sizes()
{
	const unsigned fvfs[] = {
		DX8_FVF_XYZ, DX8_FVF_XYZN, DX8_FVF_XYZNUV1, DX8_FVF_XYZNUV2, DX8_FVF_XYZNDUV1,
		DX8_FVF_XYZNDUV2, DX8_FVF_XYZDUV1, DX8_FVF_XYZDUV2, DX8_FVF_XYZUV1, DX8_FVF_XYZUV2,
		D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX8,
		D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE1(1),
		D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0),
		D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
		D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_TEX1,
		D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE,
	};

	bool ok = true;
	for (unsigned fvf : fvfs) {
		const unsigned ours = FVFInfoClass(fvf).Get_FVF_Size();
		const unsigned d3dx = D3DXGetFVFVertexSize(fvf);
		if (ours != d3dx) {
			std::fprintf(stderr, "FAILED: FVF 0x%08x size %u, D3DX says %u\n", fvf, ours, d3dx);
			ok = false;
		}
	}
	return ok;
}

bool Near(float a, float b)
{
	return std::fabs(a - b) <= 1e-4f * (1.0f + std::fabs(b));
}

// Sorting renderer: view-space centre of a bounding sphere from D3D-convention world and view.
bool Test_Sorting_Transform()
{
	Matrix3D world_3d(true), view_3d(true);
	world_3d.Rotate_Z(0.7f);
	world_3d.Translate(Vector3(3.0f, -2.0f, 5.0f));
	view_3d.Rotate_X(-0.3f);
	view_3d.Translate(Vector3(-1.0f, 4.0f, -10.0f));
	const Matrix4 world_ww(world_3d), view_ww(view_3d);

	// The renderer stores matrices transposed into D3D convention.
	const Matrix4 world = world_ww.Transpose();
	const Matrix4 view = view_ww.Transpose();
	const Vector3 center(1.5f, -0.25f, 2.0f);

	const Matrix4 world_view = (world * view).Transpose();
	Vector3 ours;
	Matrix4::Transform_Vector(world_view, center, &ours);

	const D3DXMATRIX d3dx_mtx = (const D3DXMATRIX &)world * (const D3DXMATRIX &)view;
	const D3DXVECTOR3 d3dx_in(center.X, center.Y, center.Z);
	D3DXVECTOR4 d3dx_out;
	D3DXVec3Transform(&d3dx_out, &d3dx_in, &d3dx_mtx);

	bool ok = Near(ours.X, d3dx_out.x) && Near(ours.Y, d3dx_out.y) && Near(ours.Z, d3dx_out.z);

	D3DXMATRIX transposed = d3dx_mtx;
	D3DXMatrixTranspose(&transposed, &transposed);
	const Matrix4 &d3dx_transposed = (const Matrix4 &)transposed;
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			ok &= Near(world_view[r][c], d3dx_transposed[r][c]);
		}
	}

	if (!ok) {
		std::fprintf(stderr, "FAILED: sorting transform (%f %f %f) vs D3DX (%f %f %f)\n",
			ours.X, ours.Y, ours.Z, d3dx_out.x, d3dx_out.y, d3dx_out.z);
	}
	return ok;
}
}

int main()
{
	bool ok = true;
	ok &= Test_FVF_Sizes();
	ok &= Test_Sorting_Transform();
	if (ok) {
		std::puts("D3DX replacement tests passed");
	}
	return ok ? 0 : 1;
}

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

#include "w3drender.h"

W3DRenderSystem *	W3D_Create_D3D9_Render_System();
const char *		W3D_D3D9_Error_String(HRESULT result);

#ifdef W3D_RENDERER_D3D12
W3DRenderSystem *	W3D_Create_D3D12_Render_System();
#endif

W3DRenderSystem *W3D_Create_Render_System(W3DRendererType type)
{
	switch (type) {
	case W3DRendererType::D3D9:
		return W3D_Create_D3D9_Render_System();
	case W3DRendererType::D3D12:
#ifdef W3D_RENDERER_D3D12
		return W3D_Create_D3D12_Render_System();
#else
		return nullptr;
#endif
	}
	return nullptr;
}

bool W3D_Parse_Renderer_Name(const char *name, W3DRendererType &type)
{
	if (name == nullptr) {
		return false;
	}
	if (_stricmp(name, "d3d9") == 0) {
		type = W3DRendererType::D3D9;
		return true;
	}
	if (_stricmp(name, "d3d12") == 0) {
		type = W3DRendererType::D3D12;
		return true;
	}
	return false;
}

const char *W3D_Render_Error_String(HRESULT result)
{
	return W3D_D3D9_Error_String(result);
}

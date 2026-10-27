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

// Direct3D 9 backend entry points for code that already owns an IDirect3D9 (the config
// tools enumerate adapters themselves and hand the interface to DX8Caps).

#pragma once

#ifndef W3DRENDER_D3D9_H
#define W3DRENDER_D3D9_H

#include "w3drender.h"

// Wrap an existing IDirect3D9. The wrapper takes its own reference.
W3DRenderSystem *	W3D_Wrap_D3D9_Render_System(IDirect3D9 *d3d);

#endif

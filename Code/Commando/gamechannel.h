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

//
// Filename:     gamechannel.h
// Project:      Network.lib, for Commando
// Author:       Tom Spencer-Smith
// Date:         Dec 1998
// Description:
//
//-----------------------------------------------------------------------------
#if defined(_MSV_VER)
#pragma once
#endif

#ifndef GAMECHANNEL_H
#define GAMECHANNEL_H

#include "wwdebug.h"
#include "always.h"
#include <WWOnline/RefPtr.h>
#include "refcount.h"

class cGameData;

//-----------------------------------------------------------------------------
// A game found on the LAN, as announced by cLanChat.
//-----------------------------------------------------------------------------
class cGameChannel : public RefCountClass
{
	friend class cGameChannelList;

	public:
		cGameData* Get_Game_Data(void) {WWASSERT(PGameData != nullptr); return PGameData;}

	private:
		cGameChannel(cGameData * p_game_data);
		~cGameChannel(void);

		cGameData* PGameData;
};

#endif // GAMECHANNEL_H

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

/*
** Server event layer. See serverevents.h.
*/

#include "serverevents.h"

#include "cnetwork.h"
#include "wwdebug.h"

DynamicVectorClass<ServerEventHandlerClass *>	ServerEvents::Handlers;

//-----------------------------------------------------------------------------
void
ServerEvents::Register_Handler(ServerEventHandlerClass * handler)
{
	WWASSERT(handler != nullptr);

	if (handler != nullptr && Handlers.ID(handler) == -1) {
		Handlers.Add(handler);
	}
}

//-----------------------------------------------------------------------------
void
ServerEvents::Unregister_Handler(ServerEventHandlerClass * handler)
{
	Handlers.Delete(handler);
}

//-----------------------------------------------------------------------------
void
ServerEvents::Level_Loaded(void)
{
	if (!cNetwork::I_Am_Server()) {
		return;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		Handlers[i]->On_Level_Loaded();
	}
}

//-----------------------------------------------------------------------------
void
ServerEvents::Game_Over(void)
{
	if (!cNetwork::I_Am_Server()) {
		return;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		Handlers[i]->On_Game_Over();
	}
}

//-----------------------------------------------------------------------------
void
ServerEvents::Player_Join(cPlayer * player)
{
	if (!cNetwork::I_Am_Server() || player == nullptr) {
		return;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		Handlers[i]->On_Player_Join(player);
	}
}

//-----------------------------------------------------------------------------
void
ServerEvents::Player_Leave(cPlayer * player)
{
	if (!cNetwork::I_Am_Server() || player == nullptr) {
		return;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		Handlers[i]->On_Player_Leave(player);
	}
}

//-----------------------------------------------------------------------------
void
ServerEvents::Think(void)
{
	if (!cNetwork::I_Am_Server()) {
		return;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		Handlers[i]->On_Think();
	}
}

//-----------------------------------------------------------------------------
bool
ServerEvents::Chat(cPlayer * sender, int type, int recipient_id, const WideStringClass & text)
{
	if (!cNetwork::I_Am_Server() || sender == nullptr) {
		return true;
	}

	for (int i = 0; i < Handlers.Count(); i++) {
		if (!Handlers[i]->On_Chat(sender, type, recipient_id, text)) {
			return false;
		}
	}
	return true;
}

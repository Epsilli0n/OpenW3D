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
** Server event layer. Server features (bot log, admin and chat commands, gameplay rules)
** register a ServerEventHandlerClass and get called when things happen in the game, instead
** of each feature patching the engine code where the event occurs. This is OpenW3D's
** equivalent of the TT/SSGM and Dragonade event hooks.
**
** Events only fire on the server (dedicated server or listen host), from the main thread.
** Handlers must not register or unregister handlers from inside a callback.
*/

#ifndef __SERVEREVENTS_H__
#define __SERVEREVENTS_H__

#include "vector.h"
#include "widestring.h"

class cPlayer;

//-----------------------------------------------------------------------------
class ServerEventHandlerClass
{
public:
	virtual ~ServerEventHandlerClass(void) {}

	//
	// A map finished loading and the game is about to start.
	//
	virtual void		On_Level_Loaded(void)							{}

	//
	// The game ended (time, base destruction, beacon or forced), before the map rotates.
	//
	virtual void		On_Game_Over(void)								{}

	//
	// A player finished joining and has a cPlayer and team.
	//
	virtual void		On_Player_Join(cPlayer * /* player */)		{}

	//
	// A player left or lost the connection. The cPlayer still exists (kept for scores).
	//
	virtual void		On_Player_Leave(cPlayer * /* player */)	{}

	//
	// Once per server frame while a game is running.
	//
	virtual void		On_Think(void)										{}

	//
	// A player sent a chat message (type is a TextMessageEnum; recipient_id is the player
	// ID for private messages). Return false to block it; the message is then not shown
	// to anyone and later handlers are not called. Used for !commands and mutes.
	//
	virtual bool		On_Chat(cPlayer * /* sender */, int /* type */, int /* recipient_id */,
								  const WideStringClass & /* text */)	{ return true; }

	//
	// A player asked to buy something (type is a VendorClass::PURCHASE_TYPE, definition_id
	// the preset). cost can be changed (0 makes it free). Return false to refuse; the
	// player is told "not in stock" and later handlers are not called.
	//
	virtual bool		On_Purchase(cPlayer * /* buyer */, int /* type */, int /* definition_id */,
									int & /* cost */)							{ return true; }

	//
	// A soldier was killed. killer is null for kills by the world, AI or a building, and
	// may equal victim for suicides.
	//
	virtual void		On_Kill(cPlayer * /* killer */, cPlayer * /* victim */)	{}
};

//-----------------------------------------------------------------------------
class ServerEvents
{
public:
	static void			Register_Handler(ServerEventHandlerClass * handler);
	static void			Unregister_Handler(ServerEventHandlerClass * handler);

	//
	// Called by the engine at the matching places.
	//
	static void			Level_Loaded(void);
	static void			Game_Over(void);
	static void			Player_Join(cPlayer * player);
	static void			Player_Leave(cPlayer * player);
	static void			Think(void);
	static bool			Chat(cPlayer * sender, int type, int recipient_id, const WideStringClass & text);
	static bool			Purchase(cPlayer * buyer, int type, int definition_id, int & cost);
	static void			Kill(cPlayer * killer, cPlayer * victim);

private:
	static DynamicVectorClass<ServerEventHandlerClass *>	Handlers;
};

//-----------------------------------------------------------------------------

#endif	// __SERVEREVENTS_H__

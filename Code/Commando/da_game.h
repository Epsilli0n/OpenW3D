/*	Renegade Scripts.dll
    Dragonade Game Manager
	Copyright 2017 Whitedragon, Tiberian Technologies

	This file is part of the Renegade scripts.dll
	The Renegade scripts.dll is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 2, or (at your option) any later
	version. See the file COPYING for more details.
	In addition, an exemption is given to allow Run Time Dynamic Linking of this code with any closed source module that does not contain code covered by this licence.
	Only the source code to the module(s) containing the licenced code has to be released.
*/

/*
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_game.h. So far this covers DA's startup
** (settings chain, log server), the per-map settings reload and the level loaded and game over
** messages. Game modes and game features come later.
*/

#ifndef __DA_GAME_H__
#define __DA_GAME_H__

#include "da_event.h"
#include "wwstring.h"

class DAGameManager : public DAEventClass {
public:
	//
	// Called by the server when a level finished loading; does DA's one-time startup first.
	//
	static void Init();
	static void Shutdown();
	static const char *Get_Map();
	static bool Is_Initialized();

	//
	// Engine helpers Dragonade's code uses (engine_tdb.cpp, da.h), narrow strings.
	//
	static StringClass Get_Team_Name(int Team);
	static StringClass Format_Time(unsigned long Time);
	static StringClass Get_Win_Type_Text();

private:
	virtual void Level_Loaded_Event() override;
	virtual void Game_Over_Event() override;

	static StringClass Map;
	static bool FirstMap;
};

#endif	// __DA_GAME_H__

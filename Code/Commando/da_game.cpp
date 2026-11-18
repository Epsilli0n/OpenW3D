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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_game.cpp. See da_game.h.
*/

#include "da_game.h"

#include <limits.h>

#include "da_log.h"
#include "da_settings.h"
#include "cnetwork.h"
#include "ConsoleMode.h"
#include "gamedata.h"
#include "playermanager.h"
#include "ServerSettings.h"
#include "string_ids.h"
#include "translatedb.h"

StringClass DAGameManager::Map = "";
bool DAGameManager::FirstMap = true;

static DAGameManager *Instance = nullptr;

void DA_GameLog_Init();

void DAGameManager::Init() {
	if (Instance || !cNetwork::I_Am_Server()) {
		return;
	}
	//
	// DA's singletons are never deleted: a static destructor could run after the event lists
	// it unregisters from.
	//
	Instance = new DAGameManager;
	Instance->Register_Event(DAEvent::GAMEOVER,INT_MAX);
	Instance->Register_Event(DAEvent::LEVELLOADED,INT_MAX);

	//
	// DA adds da.ini here. OpenW3D's DA settings are sections of the server's own settings file.
	//
	const char *SettingsFile = ServerSettingsClass::Get_Settings_File_Name();
	if (SettingsFile != nullptr && SettingsFile[0] != '\0') {
		DASettingsManager::Add_Settings(SettingsFile);
	}
	if (The_Game() != nullptr) {
		Map = The_Game()->Get_Map_Name();
	}

	DALogManager::Init();
	DA_GameLog_Init();
}

void DAGameManager::Shutdown() {
	DALogManager::Shutdown();
	DASettingsManager::Shutdown();
	if (Instance) {
		Instance->Clear_Events();
		Instance = nullptr;
	}
}

bool DAGameManager::Is_Initialized() {
	return Instance != nullptr;
}

const char *DAGameManager::Get_Map() {
	return Map;
}

StringClass DAGameManager::Get_Team_Name(int Team) {
	StringClass Name;
	if (Team == 0) {
		WideStringClass(TRANSLATE(IDS_MP_TEAMNAME_MISSIONS_TEAM_0)).Convert_To(Name);
	}
	else if (Team == 1) {
		WideStringClass(TRANSLATE(IDS_MP_TEAMNAME_MISSIONS_TEAM_1)).Convert_To(Name);
	}
	else {
		Name = "Neutral";
	}
	return Name;
}

StringClass DAGameManager::Format_Time(unsigned long Time) {
	StringClass Text;
	Text.Format("%02lu.%02lu.%02lu",Time/3600,(Time/60)%60,Time%60);
	return Text;
}

//
// DA's names for the win types (same values as cGameData::WinTypeEnum).
//
StringClass DAGameManager::Get_Win_Type_Text() {
	switch (The_Game()->Get_Win_Type()) {
		case 0: return "server shutdown/gameover";
		case 1: return "score limit reached";
		case 2: return "high score when time limit expired";
		case 3: return "building destruction";
		case 4: return "pedestal beacon";
		default: return "unknown";
	}
}

void DAGameManager::Game_Over_Event() {
	StringClass WinType = Get_Win_Type_Text();
	StringClass Winner = Get_Team_Name(The_Game()->Get_Winner_ID());
	StringClass Message;
	Message.Format("Current game on map %s has ended. Game was won by %s by %s, lasted %s, and had a total of %d player(s).",
		Get_Map(),Winner.Peek_Buffer(),WinType.Peek_Buffer(),Format_Time(The_Game()->Get_Game_Duration_S()).Peek_Buffer(),cPlayerManager::Count());
	ConsoleBox.Print("%s\n",Message.Peek_Buffer());
	DALogManager::Write_Log("_GENERAL","%s",Message.Peek_Buffer());
	DALogManager::Write_Log("_GAMEOVER","%s %s %u %d %s",Get_Map(),Winner.Peek_Buffer(),The_Game()->Get_Game_Duration_S(),cPlayerManager::Count(),WinType.Peek_Buffer());
}

void DAGameManager::Level_Loaded_Event() {
	FirstMap = false;
	ConsoleBox.Print("Level %s Loaded OK\n",The_Game()->Get_Map_Name().Peek_Buffer());
	DALogManager::Write_Log("_LEVEL","%s",The_Game()->Get_Map_Name().Peek_Buffer());
	Map = The_Game()->Get_Map_Name();

	//
	// DA reloads its settings every map, then lets features react (Settings_Loaded_Event).
	//
	DASettingsManager::Reload();
}

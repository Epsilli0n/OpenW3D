/*	Renegade Scripts.dll
    Dragonade Gamelog
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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_gamelog.cpp: the SSGM gamelog lines (001)
** bots read. Enabled with EnableGameLog = 1 in [General], as in DA.
**
** Done so far: the map start header ("2.03;<map>" and CONFIG) and WIN. CREATED, DESTROYED,
** DAMAGED, KILLED, ENTER and EXIT follow DA's formats once the object, damage and vehicle
** events are hooked.
*/

#include "da_event.h"
#include "da_game.h"
#include "da_log.h"
#include "da_settings.h"
#include "gamedata.h"
#include "team.h"
#include "teammanager.h"

class DAGameLogGameFeatureClass : public DAEventClass {
public:
	void Init() {
		Register_Event(DAEvent::LEVELLOADED);
		Register_Event(DAEvent::GAMEOVER);
	}

	virtual void Level_Loaded_Event() override {
		StringClass Title;
		WideStringClass(The_Game()->Get_Game_Title()).Convert_To(Title);
		DALogManager::Write_GameLog("2.03;%s",The_Game()->Get_Map_Name().Peek_Buffer());
		DALogManager::Write_GameLog("CONFIG;%u;%s",(unsigned)The_Game()->Get_Time_Limit_Minutes(),Title.Peek_Buffer());
	}

	virtual void Game_Over_Event() override {
		cTeam *Nod = cTeamManager::Find_Team(0);
		cTeam *GDI = cTeamManager::Find_Team(1);
		DALogManager::Write_GameLog("WIN;%s;%s;%d;%d",
			DAGameManager::Get_Team_Name(The_Game()->Get_Winner_ID()).Peek_Buffer(),
			DAGameManager::Get_Win_Type_Text().Peek_Buffer(),
			Nod ? (int)Nod->Get_Score() : 0,
			GDI ? (int)GDI->Get_Score() : 0);
	}
};

//
// DA loads game features when settings load (Register_Game_Feature with an enable setting).
// Until the game feature system is ported, this small loader does the same for the gamelog.
//
class DAGameLogLoaderClass : public DAEventClass {
public:
	DAGameLogLoaderClass() : Feature(nullptr) {}

	virtual void Settings_Loaded_Event() override {
		bool Enabled = DASettingsManager::Get_Bool("EnableGameLog",false);
		if (Enabled && !Feature) {
			Feature = new DAGameLogGameFeatureClass;
			Feature->Init();
			DALogManager::Write_Log("_GAMEFEATURE","load %s","DAGameLogGameFeatureClass");
		}
		else if (!Enabled && Feature) {
			delete Feature;
			Feature = nullptr;
			DALogManager::Write_Log("_GAMEFEATURE","unload %s","DAGameLogGameFeatureClass");
		}
	}

private:
	DAGameLogGameFeatureClass *Feature;
};

void DA_GameLog_Init() {
	static DAGameLogLoaderClass *Loader = nullptr;
	if (!Loader) {
		Loader = new DAGameLogLoaderClass;		// never deleted, see DAGameManager::Init
		Loader->Register_Event(DAEvent::SETTINGSLOADED);
	}
}

/*	Renegade Scripts.dll
    Dragonade Event Manager
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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_event.cpp. See da_event.h.
*/

#include "da_event.h"

#include <math.h>

#include "cnetwork.h"
#include "systimer.h"

DynamicVectorClass<DAEventStruct*> DAEventManager::Events[DAEvent::MAX];
DynamicVectorClass<DAObjectEventStruct*> DAEventManager::ObjectEvents[DAObjectEvent::MAX];
DynamicVectorClass<DAEventTimerStruct*> DAEventManager::Timers;

void DAEventManager::Register_Event(DAEventClass *Base,DAEvent::Type Type,int Priority) {
	for (int i = 0;i < Events[Type].Count();i++) {
		if (Events[Type][i]->Base == Base) {
			return;
		}
	}
	DAEventStruct *Event = new DAEventStruct;
	Event->Base = Base;
	Event->Priority = Priority;
	for (int i = 0;i < Events[Type].Count();i++) {
		if (Events[Type][i]->Priority < Priority) {
			Events[Type].Insert(i,Event);
			return;
		}
	}
	Events[Type].Add(Event);
}

void DAEventManager::Unregister_Event(DAEventClass *Base,DAEvent::Type Type) {
	for (int i = Events[Type].Count()-1;i >= 0;i--) {
		if (Events[Type][i]->Base == Base) {
			delete Events[Type][i];
			Events[Type].Delete(i);
		}
	}
}

void DAEventManager::Clear_Events(DAEventClass *Base) {
	for (int j = 0;j < DAEvent::MAX;j++) {
		Unregister_Event(Base,(DAEvent::Type)j);
	}
}

void DAEventManager::Register_Object_Event(DAEventClass *Base,DAObjectEvent::Type Type,DAObjectEvent::ObjectType ObjectType,int Priority) {
	for (int i = 0;i < ObjectEvents[Type].Count();i++) {
		if (ObjectEvents[Type][i]->Base == Base) {
			return;
		}
	}
	DAObjectEventStruct *Event = new DAObjectEventStruct;
	Event->Base = Base;
	Event->ObjectType = ObjectType;
	Event->Priority = Priority;
	for (int i = 0;i < ObjectEvents[Type].Count();i++) {
		if (ObjectEvents[Type][i]->Priority < Priority) {
			ObjectEvents[Type].Insert(i,Event);
			return;
		}
	}
	ObjectEvents[Type].Add(Event);
}

void DAEventManager::Unregister_Object_Event(DAEventClass *Base,DAObjectEvent::Type Type) {
	for (int i = ObjectEvents[Type].Count()-1;i >= 0;i--) {
		if (ObjectEvents[Type][i]->Base == Base) {
			delete ObjectEvents[Type][i];
			ObjectEvents[Type].Delete(i);
		}
	}
}

void DAEventManager::Clear_Object_Events(DAEventClass *Base) {
	for (int j = 0;j < DAObjectEvent::MAX;j++) {
		Unregister_Object_Event(Base,(DAObjectEvent::Type)j);
	}
}

void DAEventManager::Start_Timer(DAEventClass *Base,int Number,float Duration,bool Repeat,unsigned int Data) {
	DAEventTimerStruct *Timer = new DAEventTimerStruct;
	Timer->Base = Base;
	Timer->Number = Number;
	Timer->Repeat = Repeat;
	Timer->Data = Data;
	Timer->Duration = (unsigned long)(Duration*1000);
	Timer->StartTime = TIMEGETTIME();
	Timers.Add(Timer);
}

void DAEventManager::Stop_Timer(DAEventClass *Base,int Number,unsigned int Data) {
	for (int i = Timers.Count()-1;i >= 0;i--) {
		if (Timers[i]->Base == Base && Timers[i]->Number == Number && (!Data || Timers[i]->Data == Data)) {
			Timers[i]->Number = -1;
		}
	}
}

bool DAEventManager::Is_Timer(DAEventClass *Base,int Number,unsigned int Data) {
	for (int i = Timers.Count()-1;i >= 0;i--) {
		if (Timers[i]->Base == Base && Timers[i]->Number == Number && (!Data || Timers[i]->Data == Data)) {
			return true;
		}
	}
	return false;
}

void DAEventManager::Clear_Timers(DAEventClass *Base) {
	for (int i = Timers.Count()-1;i >= 0;i--) {
		if (Timers[i]->Base == Base) {
			delete Timers[i];
			Timers.Delete(i);
		}
	}
}

void DAEventManager::Think() {
	if (!cNetwork::I_Am_Server()) {
		return;
	}
	for (int i = 0;i < Events[DAEvent::THINK].Count();i++) {
		Events[DAEvent::THINK][i]->Base->Think();
	}
	for (int i = 0;i < Timers.Count();i++) {
		if (Timers[i]->Number == -1) {
			delete Timers[i];
			Timers.Delete(i);
			i--;
		}
		else if (TIMEGETTIME()-Timers[i]->StartTime >= Timers[i]->Duration) {
			DAEventTimerStruct *Timer = Timers[i];
			Timer->Base->Timer_Expired(Timer->Number,Timer->Data);
			//
			// Timer_Expired may have stopped or cleared timers, so look the timer up again.
			//
			int Index = Timers.ID(Timer);
			if (Index == -1) {
				i--;
			}
			else if (Timer->Repeat && Timer->Number != -1) {
				Timer->StartTime = TIMEGETTIME();
			}
			else {
				delete Timer;
				Timers.Delete(Index);
				i = Index-1;
			}
		}
	}
}

void DAEventManager::Settings_Loaded_Event() {
	for (int i = 0;i < Events[DAEvent::SETTINGSLOADED].Count();i++) {
		Events[DAEvent::SETTINGSLOADED][i]->Base->Settings_Loaded_Event();
	}
}

bool DAEventManager::Chat_Event(cPlayer *Player,TextMessageEnum Type,const unichar_t *Message,int ReceiverID) {
	if (!cNetwork::I_Am_Server() || !Player) {
		return true;
	}
	for (int i = 0;i < Events[DAEvent::CHAT].Count();i++) {
		if (!Events[DAEvent::CHAT][i]->Base->Chat_Event(Player,Type,Message,ReceiverID)) {
			return false;
		}
	}
	return true;
}

void DAEventManager::Player_Join_Event(cPlayer *Player) {
	if (cNetwork::I_Am_Server() && Player) {
		for (int i = 0;i < Events[DAEvent::PLAYERJOIN].Count();i++) {
			Events[DAEvent::PLAYERJOIN][i]->Base->Player_Join_Event(Player);
		}
	}
}

void DAEventManager::Player_Leave_Event(cPlayer *Player) {
	if (cNetwork::I_Am_Server() && Player) {
		for (int i = 0;i < Events[DAEvent::PLAYERLEAVE].Count();i++) {
			Events[DAEvent::PLAYERLEAVE][i]->Base->Player_Leave_Event(Player);
		}
	}
}

void DAEventManager::Level_Loaded_Event() {
	if (!cNetwork::I_Am_Server()) {
		return;
	}
	for (int i = 0;i < Events[DAEvent::LEVELLOADED].Count();i++) {
		Events[DAEvent::LEVELLOADED][i]->Base->Level_Loaded_Event();
	}
}

void DAEventManager::Game_Over_Event() {
	if (!cNetwork::I_Am_Server()) {
		return;
	}
	for (int i = 0;i < Events[DAEvent::GAMEOVER].Count();i++) {
		Events[DAEvent::GAMEOVER][i]->Base->Game_Over_Event();
	}
}

//
// Console and log output can be printed by the handlers themselves, so do not re-enter.
//
static bool InOutputEvent = false;

void DAEventManager::Console_Output_Event(const char *Output) {
	if (InOutputEvent || !Output) {
		return;
	}
	InOutputEvent = true;
	for (int i = 0;i < Events[DAEvent::CONSOLEOUTPUT].Count();i++) {
		Events[DAEvent::CONSOLEOUTPUT][i]->Base->Console_Output_Event(Output);
	}
	InOutputEvent = false;
}

void DAEventManager::Ren_Log_Event(const char *Output) {
	if (InOutputEvent || !Output) {
		return;
	}
	InOutputEvent = true;
	for (int i = 0;i < Events[DAEvent::RENLOG].Count();i++) {
		Events[DAEvent::RENLOG][i]->Base->Ren_Log_Event(Output);
	}
	InOutputEvent = false;
}

void DAEventManager::DA_Log_Event(const char *Header,const char *Output) {
	for (int i = 0;i < Events[DAEvent::DALOG].Count();i++) {
		Events[DAEvent::DALOG][i]->Base->DA_Log_Event(Header,Output);
	}
}

//
// Request events stop at the first handler that does not return PurchaseStatus_Allow. The cost
// is rounded after each handler, as in Dragonade.
//
PurchaseStatus DAEventManager::Character_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const SoldierGameObjDef *Item) {
	if (!cNetwork::I_Am_Server() || !Player) {
		return PurchaseStatus_Allow;
	}
	for (int i = 0;i < Events[DAEvent::CHARACTERPURCHASEREQUEST].Count();i++) {
		PurchaseStatus Return = Events[DAEvent::CHARACTERPURCHASEREQUEST][i]->Base->Character_Purchase_Request_Event(Base,Player,Cost,Item);
		Cost = floorf(Cost+0.5f);
		if (Return != PurchaseStatus_Allow) {
			if (Return == PurchaseStatus_Granted) {
				Character_Purchase_Event(Player,Cost,Item);
			}
			return Return;
		}
	}
	return PurchaseStatus_Allow;
}

PurchaseStatus DAEventManager::Vehicle_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const VehicleGameObjDef *Item) {
	if (!cNetwork::I_Am_Server() || !Player) {
		return PurchaseStatus_Allow;
	}
	for (int i = 0;i < Events[DAEvent::VEHICLEPURCHASEREQUEST].Count();i++) {
		PurchaseStatus Return = Events[DAEvent::VEHICLEPURCHASEREQUEST][i]->Base->Vehicle_Purchase_Request_Event(Base,Player,Cost,Item);
		Cost = floorf(Cost+0.5f);
		if (Return != PurchaseStatus_Allow) {
			if (Return == PurchaseStatus_Granted) {
				Vehicle_Purchase_Event(Player,Cost,Item);
			}
			return Return;
		}
	}
	return PurchaseStatus_Allow;
}

PurchaseStatus DAEventManager::PowerUp_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const PowerUpGameObjDef *Item) {
	if (!cNetwork::I_Am_Server() || !Player) {
		return PurchaseStatus_Allow;
	}
	for (int i = 0;i < Events[DAEvent::POWERUPPURCHASEREQUEST].Count();i++) {
		PurchaseStatus Return = Events[DAEvent::POWERUPPURCHASEREQUEST][i]->Base->PowerUp_Purchase_Request_Event(Base,Player,Cost,Item);
		Cost = floorf(Cost+0.5f);
		if (Return != PurchaseStatus_Allow) {
			if (Return == PurchaseStatus_Granted) {
				PowerUp_Purchase_Event(Player,Cost,Item);
			}
			return Return;
		}
	}
	return PurchaseStatus_Allow;
}

void DAEventManager::Character_Purchase_Event(cPlayer *Player,float Cost,const SoldierGameObjDef *Item) {
	for (int i = 0;i < Events[DAEvent::CHARACTERPURCHASE].Count();i++) {
		Events[DAEvent::CHARACTERPURCHASE][i]->Base->Character_Purchase_Event(Player,Cost,Item);
	}
}

void DAEventManager::Vehicle_Purchase_Event(cPlayer *Player,float Cost,const VehicleGameObjDef *Item) {
	for (int i = 0;i < Events[DAEvent::VEHICLEPURCHASE].Count();i++) {
		Events[DAEvent::VEHICLEPURCHASE][i]->Base->Vehicle_Purchase_Event(Player,Cost,Item);
	}
}

void DAEventManager::PowerUp_Purchase_Event(cPlayer *Player,float Cost,const PowerUpGameObjDef *Item) {
	for (int i = 0;i < Events[DAEvent::POWERUPPURCHASE].Count();i++) {
		Events[DAEvent::POWERUPPURCHASE][i]->Base->PowerUp_Purchase_Event(Player,Cost,Item);
	}
}

bool DAEventManager::Refill_Event(cPlayer *Player) {
	if (!cNetwork::I_Am_Server() || !Player) {
		return true;
	}
	for (int i = 0;i < Events[DAEvent::REFILL].Count();i++) {
		if (!Events[DAEvent::REFILL][i]->Base->Refill_Event(Player)) {
			return false;
		}
	}
	return true;
}

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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_event.h. The class, enum and
** function names and the event signatures follow Dragonade so DA features can be ported
** with few changes. Events are raised directly by the OpenW3D server code instead of
** through tt.dll hooks. Only events whose engine hook exists are dispatched; the rest of
** DA's events are listed in DAEvent::Type and get dispatched as their hooks are added.
**
** Changes from Dragonade:
** - DAObjectEvent::ARMED, POWERUP and SIMPLE are 0x100, 0x200 and 0x400. DA wrote them as
**   0x160, 0x320 and 0x640, which overlap the other flags.
** - Unregister_Event and Clear_Events no longer skip the entry after a removed one.
** - DAEventClass unregisters its events and timers when destroyed.
** - Events only fire on the server.
** - Chat text is unichar_t (wchar_t on Windows, char16_t with ICU) instead of wchar_t.
*/

#ifndef __DA_EVENT_H__
#define __DA_EVENT_H__

#include "vector.h"
#include "sctextobj.h"
#include "widestring.h"

class cPlayer;
class DAEventClass;
class BaseControllerClass;
class SoldierGameObjDef;
class VehicleGameObjDef;
class PowerUpGameObjDef;

//
// Same values as TT's PurchaseStatus (engine_ttdef.h).
//
enum PurchaseStatus
{
	PurchaseStatus_AllowNoSpawn = -3,		// Purchase is allowed as normal, without spawning the vehicle. (vehicles only)
	PurchaseStatus_AllowFree = -2,			// Purchase is allowed as normal, without charging the player.
	PurchaseStatus_Allow = -1,					// Purchase is allowed as normal.
	PurchaseStatus_Granted,						// "Purchase request granted." The handler did the purchase itself.
	PurchaseStatus_Pending,						// "Transaction pending."
	PurchaseStatus_InsufficientFunds,		// "You have insufficient funds for this purchase."
	PurchaseStatus_FactoryUnavailable,		// "The factory is not presently available."
	PurchaseStatus_OutOfStock,					// "This item is not presently in stock."
};

namespace DAChatType {
	enum Type {
		ALL = 0,
		PUBLIC = 0x1,
		TEAM = 0x2,
		PRIVATE = 0x4,
		TMSG = 0x8
	};
};
inline DAChatType::Type operator|(DAChatType::Type First,DAChatType::Type Second) {
	return (DAChatType::Type)(+First | +Second);
}

namespace DAAccessLevel {
	enum Level {
		NONE,
		VIP,
		TEMPMODERATOR,
		HALFMODERATOR,
		FULLMODERATOR,
		SUPERMODERATOR, //Generic name for Officer or Division Leader.
		ADMINISTRATOR
	};
};

namespace DAEvent {
	enum Type {
		SETTINGSLOADED,
		CHAT,
		CHATCOMMAND,
		KEYHOOK,
		HOSTCHAT,
		RADIO,
		CONNECTIONREQUEST,
		PLAYERPREJOIN,
		PLAYERJOIN,
		PLAYERLEAVE,
		PLAYERLOADED,
		NAMECHANGE,
		LEVELLOADED,
		REMIX,
		REBALANCE,
		SWAP,
		GAMEOVER,
		CONSOLEOUTPUT,
		RENLOG,
		DALOG,
		VEHICLEPURCHASEREQUEST,
		CHARACTERPURCHASEREQUEST,
		POWERUPPURCHASEREQUEST,
		CUSTOMPURCHASEREQUEST,
		VEHICLEPURCHASE,
		CHARACTERPURCHASE,
		POWERUPPURCHASE,
		CUSTOMPURCHASE,
		REFILL,
		SUICIDE,
		TEAMCHANGEREQUEST,
		TEAMCHANGE,
		VEHICLEENTRYREQUEST,
		VEHICLEENTER,
		VEHICLEEXIT,
		POWERUPGRANTREQUEST,
		POWERUPGRANT,
		ADDWEAPONREQUEST,
		ADDWEAPON,
		REMOVEWEAPON,
		CLEARWEAPONS,
		BEACONDEPLOY,
		BEACONDETONATE,
		C4DETONATEREQUEST,
		C4DETONATE,
		CHANGECHARACTER,
		VEHICLEFLIP,
		REQUESTVEHICLE,
		THINK,
		DIALOG,
		MAX
	};
};

namespace DAObjectEvent {
	enum Type {
		CREATED,
		DESTROYED,
		KILLDEALT, //These allow you to choose whether ObjectType applies to the object being killed or the one doing the killing.
		KILLRECEIVED,
		STOCKCLIENTDAMAGEREQUEST,
		TTCLIENTDAMAGEREQUEST,
		DAMAGEDEALTREQUEST,
		DAMAGERECEIVEDREQUEST,
		DAMAGEDEALT,
		DAMAGERECEIVED,
		CUSTOM,
		POKE,
		ZONEENTER,
		ZONEEXIT,
		MAX
	};
	enum ObjectType {
		ALL = 0x1,
		PLAYER = 0x2,
		DAMAGEABLE = 0x4,
		SOLDIER = 0x8,
		VEHICLE = 0x10,
		C4 = 0x20,
		BEACON = 0x40,
		BUILDING = 0x80,
		ARMED = 0x100,
		POWERUP = 0x200,
		SIMPLE = 0x400
	};
};
inline DAObjectEvent::ObjectType operator|(DAObjectEvent::ObjectType First,DAObjectEvent::ObjectType Second) {
	return (DAObjectEvent::ObjectType)(+First | +Second);
}

struct DAEventStruct {
	DAEventClass *Base;
	int Priority;
};

struct DAObjectEventStruct {
	DAEventClass *Base;
	DAObjectEvent::ObjectType ObjectType;
	int Priority;
};

struct DAEventTimerStruct {
	DAEventClass *Base;
	unsigned long StartTime;
	unsigned long Duration;
	bool Repeat;
	int Number;
	unsigned int Data;
};

class DAEventManager {
public:
	//
	// Raised by the OpenW3D server code.
	//
	static void Settings_Loaded_Event();
	static bool Chat_Event(cPlayer *Player,TextMessageEnum Type,const unichar_t *Message,int ReceiverID);
	static void Player_Join_Event(cPlayer *Player);
	static void Player_Leave_Event(cPlayer *Player);
	static void Level_Loaded_Event();
	static void Game_Over_Event();
	static void Console_Output_Event(const char *Output);
	static void Ren_Log_Event(const char *Output);
	static void DA_Log_Event(const char *Header,const char *Output);
	static PurchaseStatus Character_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const SoldierGameObjDef *Item);
	static PurchaseStatus Vehicle_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const VehicleGameObjDef *Item);
	static PurchaseStatus PowerUp_Purchase_Request_Event(BaseControllerClass *Base,cPlayer *Player,float &Cost,const PowerUpGameObjDef *Item);
	static void Character_Purchase_Event(cPlayer *Player,float Cost,const SoldierGameObjDef *Item);
	static void Vehicle_Purchase_Event(cPlayer *Player,float Cost,const VehicleGameObjDef *Item);
	static void PowerUp_Purchase_Event(cPlayer *Player,float Cost,const PowerUpGameObjDef *Item);
	static bool Refill_Event(cPlayer *Player);
	static void Think();

	static void Register_Event(DAEventClass *Base,DAEvent::Type Type,int Priority);
	static void Unregister_Event(DAEventClass *Base,DAEvent::Type Type);
	static void Clear_Events(DAEventClass *Base);

	static void Register_Object_Event(DAEventClass *Base,DAObjectEvent::Type Type,DAObjectEvent::ObjectType ObjectType,int Priority);
	static void Unregister_Object_Event(DAEventClass *Base,DAObjectEvent::Type Type);
	static void Clear_Object_Events(DAEventClass *Base);

	static void Start_Timer(DAEventClass *Base,int Number,float Duration,bool Repeat,unsigned int Data);
	static void Stop_Timer(DAEventClass *Base,int Number,unsigned int Data);
	static bool Is_Timer(DAEventClass *Base,int Number,unsigned int Data);
	static void Clear_Timers(DAEventClass *Base);

private:
	static DynamicVectorClass<DAEventStruct*> Events[DAEvent::MAX];
	static DynamicVectorClass<DAObjectEventStruct*> ObjectEvents[DAObjectEvent::MAX];
	static DynamicVectorClass<DAEventTimerStruct*> Timers;
};

class DAEventClass {
public:
	virtual ~DAEventClass() {
		Clear_Events();
		Clear_Object_Events();
		Clear_Timers();
	}

	inline void Register_Event(DAEvent::Type Type,int Priority = 0) {
		DAEventManager::Register_Event(this,Type,Priority);
	}
	inline void Unregister_Event(DAEvent::Type Type) {
		DAEventManager::Unregister_Event(this,Type);
	}
	inline void Clear_Events() {
		DAEventManager::Clear_Events(this);
	}
	inline void Register_Object_Event(DAObjectEvent::Type Type,DAObjectEvent::ObjectType ObjectType,int Priority = 0) {
		DAEventManager::Register_Object_Event(this,Type,ObjectType,Priority);
	}
	inline void Unregister_Object_Event(DAObjectEvent::Type Type) {
		DAEventManager::Unregister_Object_Event(this,Type);
	}
	inline void Clear_Object_Events() {
		DAEventManager::Clear_Object_Events(this);
	}
	inline void Start_Timer(int Number,float Duration,bool Repeat = false,unsigned int Data = 0) {
		DAEventManager::Start_Timer(this,Number,Duration,Repeat,Data);
	}
	inline void Stop_Timer(int Number,unsigned int Data = 0) {
		DAEventManager::Stop_Timer(this,Number,Data);
	}
	inline bool Is_Timer(int Number,unsigned int Data = 0) {
		return DAEventManager::Is_Timer(this,Number,Data);
	}
	inline void Clear_Timers() {
		DAEventManager::Clear_Timers(this);
	}

	virtual void Settings_Loaded_Event() { }
	virtual bool Chat_Event(cPlayer * /*Player*/,TextMessageEnum /*Type*/,const unichar_t * /*Message*/,int /*ReceiverID*/) { return true; }
	virtual void Player_Join_Event(cPlayer * /*Player*/) { }
	virtual void Player_Leave_Event(cPlayer * /*Player*/) { }
	virtual void Level_Loaded_Event() { }
	virtual void Game_Over_Event() { }
	virtual void Console_Output_Event(const char * /*Output*/) { }
	virtual void Ren_Log_Event(const char * /*Output*/) { }
	virtual void DA_Log_Event(const char * /*Header*/,const char * /*Output*/) { }
	virtual PurchaseStatus Character_Purchase_Request_Event(BaseControllerClass * /*Base*/,cPlayer * /*Player*/,float & /*Cost*/,const SoldierGameObjDef * /*Item*/) { return PurchaseStatus_Allow; }
	virtual PurchaseStatus Vehicle_Purchase_Request_Event(BaseControllerClass * /*Base*/,cPlayer * /*Player*/,float & /*Cost*/,const VehicleGameObjDef * /*Item*/) { return PurchaseStatus_Allow; }
	virtual PurchaseStatus PowerUp_Purchase_Request_Event(BaseControllerClass * /*Base*/,cPlayer * /*Player*/,float & /*Cost*/,const PowerUpGameObjDef * /*Item*/) { return PurchaseStatus_Allow; }
	virtual void Character_Purchase_Event(cPlayer * /*Player*/,float /*Cost*/,const SoldierGameObjDef * /*Item*/) { }
	virtual void Vehicle_Purchase_Event(cPlayer * /*Player*/,float /*Cost*/,const VehicleGameObjDef * /*Item*/) { }
	virtual void PowerUp_Purchase_Event(cPlayer * /*Player*/,float /*Cost*/,const PowerUpGameObjDef * /*Item*/) { }
	virtual bool Refill_Event(cPlayer * /*Player*/) { return true; }
	virtual void Think() { }
	virtual void Timer_Expired(int /*Number*/,unsigned int /*Data*/) { }
};

#endif	// __DA_EVENT_H__

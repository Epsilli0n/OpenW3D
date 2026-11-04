/*	Renegade Scripts.dll
    Dragonade Settings Manager
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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_settings.cpp. See da_settings.h.
*/

#include "da_settings.h"

#include "ini.h"
#include "inisup.h"
#include "rawfile.h"
#include "da_event.h"
#include "da_game.h"
#include "da_log.h"
#include "ConsoleMode.h"

DynamicVectorClass<DASettingsClass*> DASettingsManager::Settings;

DASettingsClass::DASettingsClass(const char *Name) : FileName(Name), INI(nullptr), LastModTime(0) {
	Load();
	RawFileClass File(FileName);
	if (File.Is_Available()) {
		File.Open();
		LastModTime = File.Get_Date_Time();
		File.Close();
	}
}

DASettingsClass::~DASettingsClass() {
	delete INI;
	INI = nullptr;
}

void DASettingsClass::Load() {
	//
	// RawFileClass reads the file from the server folder; INIClass(const char *) would go
	// through the game's file factory, which looks in the data folders.
	//
	INIClass *Temp = INI;
	RawFileClass File(FileName);
	INI = new INIClass(File);
	delete Temp;
}

const INIClass *DASettingsClass::Get_INI() const {
	return INI;
}

const char *DASettingsClass::Get_File_Name() const {
	return FileName;
}

void DASettingsClass::Reload() {
	RawFileClass File(FileName);
	if (File.Is_Available()) {
		File.Open();
		unsigned long Time = File.Get_Date_Time();
		File.Close();
		if (Time != LastModTime) {
			LastModTime = Time;
			ConsoleBox.Print("New settings detected and loaded from \"%s\"!\n",Get_File_Name());
			DALogManager::Write_Log("_GENERAL","New settings detected and loaded from \"%s\"!",Get_File_Name());
		}
	}
	Load();
}

void DASettingsClass::Reload_Silent() {
	Load();
}

int DASettingsClass::Get_Int(const char *Entry,int Default) const {
	return Get_Int("General",DAGameManager::Get_Map(),Entry,Default);
}

int DASettingsClass::Get_Int(const char *Section,const char *Entry,int Default) const {
	StringClass MapSection;
	MapSection.Format("%s_%s",DAGameManager::Get_Map(),Section);
	return Get_Int(Section,MapSection,Entry,Default);
}

int DASettingsClass::Get_Int(const char *Section1,const char *Section2,const char *Entry,int Default) const {
	return INI->Get_Int(Section2,Entry,INI->Get_Int(Section1,Entry,Default));
}

float DASettingsClass::Get_Float(const char *Entry,float Default) const {
	return Get_Float("General",DAGameManager::Get_Map(),Entry,Default);
}

float DASettingsClass::Get_Float(const char *Section,const char *Entry,float Default) const {
	StringClass MapSection;
	MapSection.Format("%s_%s",DAGameManager::Get_Map(),Section);
	return Get_Float(Section,MapSection,Entry,Default);
}

float DASettingsClass::Get_Float(const char *Section1,const char *Section2,const char *Entry,float Default) const {
	return INI->Get_Float(Section2,Entry,INI->Get_Float(Section1,Entry,Default));
}

bool DASettingsClass::Get_Bool(const char *Entry,bool Default) const {
	return Get_Bool("General",DAGameManager::Get_Map(),Entry,Default);
}

bool DASettingsClass::Get_Bool(const char *Section,const char *Entry,bool Default) const {
	StringClass MapSection;
	MapSection.Format("%s_%s",DAGameManager::Get_Map(),Section);
	return Get_Bool(Section,MapSection,Entry,Default);
}

bool DASettingsClass::Get_Bool(const char *Section1,const char *Section2,const char *Entry,bool Default) const {
	return INI->Get_Bool(Section2,Entry,INI->Get_Bool(Section1,Entry,Default));
}

StringClass &DASettingsClass::Get_String(StringClass &Buffer,const char *Entry,const char *Default) const {
	return Get_String(Buffer,"General",DAGameManager::Get_Map(),Entry,Default);
}

StringClass &DASettingsClass::Get_String(StringClass &Buffer,const char *Section,const char *Entry,const char *Default) const {
	StringClass MapSection;
	MapSection.Format("%s_%s",DAGameManager::Get_Map(),Section);
	return Get_String(Buffer,Section,MapSection,Entry,Default);
}

StringClass &DASettingsClass::Get_String(StringClass &Buffer,const char *Section1,const char *Section2,const char *Entry,const char *Default) const {
	StringClass General;
	INI->Get_String(General,Section1,Entry,Default);
	INI->Get_String(Buffer,Section2,Entry,General);
	return Buffer;
}

INISection *DASettingsClass::Get_Section(const char *Section) const {
	StringClass MapSection;
	MapSection.Format("%s_%s",DAGameManager::Get_Map(),Section);
	if (INISection *Sect = INI->Find_Section(MapSection)) {
		return Sect;
	}
	return INI->Find_Section(Section);
}



void DASettingsManager::Shutdown() {
	for (int i = 0;i < Settings.Count();i++) {
		delete Settings[i];
	}
	Settings.Delete_All();
}

void DASettingsManager::Reload() {
	for (int i = 0;i < Settings.Count();i++) {
		Settings[i]->Reload(); //Reload all settings files.
	}
	DAEventManager::Settings_Loaded_Event(); //Then trigger events.
}

void DASettingsManager::Reload_Silent() {
	for (int i = 0;i < Settings.Count();i++) {
		Settings[i]->Reload_Silent(); //Reload all files without triggering events.
	}
}

void DASettingsManager::Add_Settings(const char *Name) {
	for (int i = 0;i < Settings.Count();i++) { //Check if we already have this settings file so we don't add duplicates.
		if (!stricmp(Settings[i]->Get_File_Name(),Name)) {
			Settings[i]->Reload_Silent();
			return;
		}
	}
	Settings.Add(new DASettingsClass(Name));
}

void DASettingsManager::Remove_Settings(const char *Name) {
	for (int i = 0;i < Settings.Count();i++) {
		if (!stricmp(Settings[i]->Get_File_Name(),Name)) {
			delete Settings[i];
			Settings.Delete(i);
			return;
		}
	}
}

const DASettingsClass *DASettingsManager::Get_Settings(const char *Name) {
	for (int i = 0;i < Settings.Count();i++) {
		if (!stricmp(Settings[i]->Get_File_Name(),Name)) {
			return Settings[i];
		}
	}
	return nullptr;
}

const DASettingsClass *DASettingsManager::Get_Settings(int Position) {
	return Settings[Position];
}

int DASettingsManager::Get_Settings_Count() {
	return Settings.Count();
}

//
// Each later file overrides the earlier ones: the previous result is the next default.
//
int DASettingsManager::Get_Int(const char *Entry,int Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Int(Entry,Default);
	}
	return Default;
}

int DASettingsManager::Get_Int(const char *Section,const char *Entry,int Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Int(Section,Entry,Default);
	}
	return Default;
}

float DASettingsManager::Get_Float(const char *Entry,float Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Float(Entry,Default);
	}
	return Default;
}

float DASettingsManager::Get_Float(const char *Section,const char *Entry,float Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Float(Section,Entry,Default);
	}
	return Default;
}

bool DASettingsManager::Get_Bool(const char *Entry,bool Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Bool(Entry,Default);
	}
	return Default;
}

bool DASettingsManager::Get_Bool(const char *Section,const char *Entry,bool Default) {
	for (int i = 0;i < Settings.Count();i++) {
		Default = Settings[i]->Get_Bool(Section,Entry,Default);
	}
	return Default;
}

StringClass &DASettingsManager::Get_String(StringClass &Buffer,const char *Entry,const char *Default) {
	Buffer = Default;
	for (int i = 0;i < Settings.Count();i++) {
		StringClass Previous = Buffer;
		Settings[i]->Get_String(Buffer,Entry,Previous);
	}
	return Buffer;
}

StringClass &DASettingsManager::Get_String(StringClass &Buffer,const char *Section,const char *Entry,const char *Default) {
	Buffer = Default;
	for (int i = 0;i < Settings.Count();i++) {
		StringClass Previous = Buffer;
		Settings[i]->Get_String(Buffer,Section,Entry,Previous);
	}
	return Buffer;
}

INISection *DASettingsManager::Get_Section(const char *Section) {
	for (int i = Settings.Count()-1;i >= 0;i--) {
		if (INISection *Sect = Settings[i]->Get_Section(Section)) {
			return Sect;
		}
	}
	return nullptr;
}

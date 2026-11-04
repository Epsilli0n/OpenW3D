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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_settings.h.
**
** Dragonade reads da.ini (and a da_<mode>.ini per game mode). OpenW3D keeps all server settings
** in the server.ini given to --startserver, so DA's sections live there under their DA names:
** [General] for da.ini's general settings, [<map file>] such as [C&C_Field.mix] for per-map
** overrides of those, and [<Section>] with [<map file>_<Section>] overriding it for feature
** sections. The lookup order inside one file is DA's: map section first, then the general one.
** Further files (game mode layers) override earlier ones, as in DA.
*/

#ifndef __DA_SETTINGS_H__
#define __DA_SETTINGS_H__

#include "vector.h"
#include "wwstring.h"

class INIClass;
struct INISection;

class DASettingsClass {
public:
	DASettingsClass(const char *Name);
	~DASettingsClass();
	const INIClass *Get_INI() const;
	const char *Get_File_Name() const;
	void Reload();
	void Reload_Silent();

	int Get_Int(const char *Entry,int Default) const;
	int Get_Int(const char *Section,const char *Entry,int Default) const;
	int Get_Int(const char *Section1,const char *Section2,const char *Entry,int Default) const;
	float Get_Float(const char *Entry,float Default) const;
	float Get_Float(const char *Section,const char *Entry,float Default) const;
	float Get_Float(const char *Section1,const char *Section2,const char *Entry,float Default) const;
	bool Get_Bool(const char *Entry,bool Default) const;
	bool Get_Bool(const char *Section,const char *Entry,bool Default) const;
	bool Get_Bool(const char *Section1,const char *Section2,const char *Entry,bool Default) const;
	StringClass &Get_String(StringClass &Buffer,const char *Entry,const char *Default) const;
	StringClass &Get_String(StringClass &Buffer,const char *Section,const char *Entry,const char *Default) const;
	StringClass &Get_String(StringClass &Buffer,const char *Section1,const char *Section2,const char *Entry,const char *Default) const;
	INISection *Get_Section(const char *Section) const;

private:
	DASettingsClass(const DASettingsClass &) = delete;
	DASettingsClass &operator=(const DASettingsClass &) = delete;
	void Load();

	StringClass FileName;
	INIClass *INI;
	unsigned long LastModTime;
};

class DASettingsManager {
public:
	static void Shutdown();
	static void Reload();
	static void Reload_Silent();
	static void Add_Settings(const char *Name);
	static void Remove_Settings(const char *Name);
	static const DASettingsClass *Get_Settings(const char *Name);
	static const DASettingsClass *Get_Settings(int Position);
	static int Get_Settings_Count();

	static int Get_Int(const char *Entry,int Default);
	static int Get_Int(const char *Section,const char *Entry,int Default);
	static float Get_Float(const char *Entry,float Default);
	static float Get_Float(const char *Section,const char *Entry,float Default);
	static bool Get_Bool(const char *Entry,bool Default);
	static bool Get_Bool(const char *Section,const char *Entry,bool Default);
	static StringClass &Get_String(StringClass &Buffer,const char *Entry,const char *Default);
	static StringClass &Get_String(StringClass &Buffer,const char *Section,const char *Entry,const char *Default);
	static INISection *Get_Section(const char *Section);

private:
	static DynamicVectorClass<DASettingsClass*> Settings;
};

#endif	// __DA_SETTINGS_H__

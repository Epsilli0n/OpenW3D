/*	Renegade Scripts.dll
    Dragonade Log Manager
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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_log.h (itself based on danpaul88's SSGM
** 4.0 log server). This is the TCP stream server bots such as BRenBot read.
**
** Settings, in server.ini [General] (read once, at server start):
**   LogPort = 7025        TCP port. 0 or missing turns the log server off (DA's default).
**   LogIP = 127.0.0.1     OpenW3D addition: address to listen on. DA listens on every address
**                         with no password, and whatever a client sends runs as a console
**                         command, so OpenW3D listens on loopback unless this names another
**                         address (0.0.0.0 for all, like DA).
**
** Records, each ending with a NUL byte, as in DA:
**   000[HH:MM:SS] _HEADER message   DA log line (Write_Log), sent again with 001 in front
**   001[HH:MM:SS] message           gamelog (Write_GameLog)
**   002[HH:MM:SS] text              copy of the server log file (renlog)
**   003text                         console output
** Lines a client sends, ending in \n (\r\n accepted), run as server console commands.
**
** OpenW3D addition: output a client cannot take yet is queued instead of dropped, and a client
** that falls more than 1 MB behind is disconnected.
*/

#ifndef __DA_LOG_H__
#define __DA_LOG_H__

#include "da_event.h"
#include "wwformat.h"

class DALogManager : public DAEventClass {
public:
	static void Init();
	static void Shutdown();
	static void Write_Log(const char *Header,const char *Format,...) OPENW3D_PRINTF_VARARG_FUNC(2);
	static void Write_GameLog(const char *Format,...) OPENW3D_PRINTF_VARARG_FUNC(1);
	static void Send(const char *Data);
	static bool Is_Listening();

private:
	virtual void Think() override;
	virtual void Ren_Log_Event(const char *Output) override;
	virtual void Console_Output_Event(const char *Output) override;
};

#endif	// __DA_LOG_H__

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
** Ported to OpenW3D (GPL v3) from Dragonade 1.11's da_log.cpp. See da_log.h.
*/

//Based on code by danpaul88 from SSGM 4.0.

#include "da_log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "da_settings.h"
#include "consolefunction.h"
#include "ConsoleMode.h"

namespace {

struct Connection {
	SOCKET Socket;
	char Buffer[512];
	int BufferFilled;
	std::string Pending;
	bool Dead;			// a send failed or the client fell too far behind; closed in Think
};

enum { MAX_PENDING_BYTES = 1024 * 1024 };

SOCKET ListenSocket = INVALID_SOCKET;
DynamicVectorClass<Connection*> Connections;
DALogManager *Instance = nullptr;

void Close_Connection(int Index) {
	closesocket(Connections[Index]->Socket);
	delete Connections[Index];
	Connections.Delete(Index);
}

//
// Send what is queued. Returns false if the connection failed.
//
bool Flush(Connection *c) {
	while (!c->Pending.empty()) {
		int Sent = send(c->Socket,c->Pending.data(),(int)c->Pending.size(),0);
		if (Sent == SOCKET_ERROR) {
			return WSAGetLastError() == WSAEWOULDBLOCK;
		}
		c->Pending.erase(0,Sent);
	}
	return true;
}

//
// DA's timestamp: GetTimeFormat with "'['HH':'mm':'ss']'", local time, 24 hours.
//
void Format_Time(char *Time,int Size) {
	memset(Time,0,Size);
	Time[0] = '?';
	GetTimeFormatA(LOCALE_SYSTEM_DEFAULT,TIME_FORCE24HOURFORMAT,nullptr,"'['HH':'mm':'ss']'",Time,Size-1);
}

}

void DALogManager::Init() {
	if (ListenSocket != INVALID_SOCKET) {
		return;
	}
	int Port = DASettingsManager::Get_Int("LogPort",0);
	if (Port <= 0 || Port > 0xFFFF) {
		return;
	}
	StringClass IP;
	DASettingsManager::Get_String(IP,"LogIP","127.0.0.1");

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2,2),&wsaData) != 0) {
		ConsoleBox.Print("Log server: Winsock unavailable\n");
		return;
	}
	sockaddr_in SockAddr;
	memset(&SockAddr,0,sizeof(SockAddr));
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons((u_short)Port);
	if (inet_pton(AF_INET,IP,&SockAddr.sin_addr) != 1) {
		ConsoleBox.Print("Log server: LogIP '%s' is not an IPv4 address, not listening\n",IP.Peek_Buffer());
		WSACleanup();
		return;
	}
	ListenSocket = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	u_long NonBlocking = 1;
	if (ListenSocket == INVALID_SOCKET ||
		 ioctlsocket(ListenSocket,FIONBIO,&NonBlocking) != 0 ||
		 bind(ListenSocket,(sockaddr *)&SockAddr,sizeof(SockAddr)) != 0 ||
		 listen(ListenSocket,3) != 0) {
		ConsoleBox.Print("Log server: cannot listen on %s:%d (error %d)\n",IP.Peek_Buffer(),Port,WSAGetLastError());
		if (ListenSocket != INVALID_SOCKET) {
			closesocket(ListenSocket);
			ListenSocket = INVALID_SOCKET;
		}
		WSACleanup();
		return;
	}
	ConsoleBox.Print("Log server listening on %s:%d\n",IP.Peek_Buffer(),Port);

	if (!Instance) {
		Instance = new DALogManager;		// never deleted, see DAGameManager::Init
	}
	Instance->Register_Event(DAEvent::THINK);
	Instance->Register_Event(DAEvent::CONSOLEOUTPUT);
	Instance->Register_Event(DAEvent::RENLOG);
}

void DALogManager::Shutdown() {
	while (Connections.Count()) {
		Close_Connection(Connections.Count()-1);
	}
	if (ListenSocket != INVALID_SOCKET) {
		closesocket(ListenSocket);
		ListenSocket = INVALID_SOCKET;
		WSACleanup();
	}
	if (Instance) {
		Instance->Clear_Events();
	}
}

bool DALogManager::Is_Listening() {
	return ListenSocket != INVALID_SOCKET;
}

void DALogManager::Think() {
	SOCKET Client = accept(ListenSocket,nullptr,nullptr);
	if (Client != INVALID_SOCKET) {
		u_long NonBlocking = 1;
		ioctlsocket(Client,FIONBIO,&NonBlocking);
		Connection *c = new Connection;
		c->BufferFilled = 0;
		c->Dead = false;
		c->Socket = Client;
		c->Buffer[sizeof(c->Buffer)-1] = '\0';
		Connections.Add(c);
	}
	for (int i = 0;i < Connections.Count();i++) {
		Connection *c = Connections[i];
		if (c->Dead || !Flush(c)) {
			Close_Connection(i--);
			continue;
		}
		//
		// A full buffer without a newline is a line too long to use; drop it instead of reading
		// 0 bytes, which DA took for a closed connection.
		//
		if (c->BufferFilled >= (int)sizeof(c->Buffer)-1) {
			c->BufferFilled = 0;
		}
		int Receive = recv(c->Socket,c->Buffer+c->BufferFilled,(int)sizeof(c->Buffer)-1-c->BufferFilled,0);
		if (Receive == 0 || (Receive == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
			Close_Connection(i--);
		}
		else if (Receive > 0) {
			char *LineStartPos = c->Buffer;
			char *EndPos = c->Buffer+c->BufferFilled+Receive;
			for (;;) {
				char *LineEndPos = (char*)memchr(LineStartPos,'\n',EndPos-LineStartPos);
				if (!LineEndPos) break;
				*LineEndPos = '\0';
				if (LineEndPos-LineStartPos > 0 && *(LineEndPos-1) == '\r') *(LineEndPos-1) = '\0';
				if (*LineStartPos) {
					//
					// The command's console output goes back out through Send, which only marks
					// failed connections, so c stays valid here.
					//
					ConsoleFunctionManager::Parse_Input(LineStartPos);
				}
				LineStartPos = LineEndPos+1;
			}
			c->BufferFilled = (int)(EndPos-LineStartPos);
			memmove(c->Buffer,LineStartPos,EndPos-LineStartPos);
		}
	}
}

void DALogManager::Write_Log(const char *Header,const char *Format,...) {
	char Buffer[526];
	char Time[64];
	Format_Time(Time,sizeof(Time));
	int Len = snprintf(Buffer,sizeof(Buffer),"000%s %s ",Time,Header);
	if (Len < 0 || Len >= (int)sizeof(Buffer)) {
		return;
	}
	va_list arg_list;
	va_start(arg_list,Format);
	vsnprintf(Buffer+Len,sizeof(Buffer)-1-Len,Format,arg_list);
	va_end(arg_list);
	Buffer[sizeof(Buffer)-1] = '\0';
	DAEventManager::DA_Log_Event(Header,Buffer+Len);
	Send(Buffer);
	Buffer[2] = '1';
	Send(Buffer);
}

void DALogManager::Write_GameLog(const char *Format,...) {
	char Buffer[526];
	char Time[64];
	Format_Time(Time,sizeof(Time));
	int Len = snprintf(Buffer,sizeof(Buffer),"001%s ",Time);
	if (Len < 0 || Len >= (int)sizeof(Buffer)) {
		return;
	}
	va_list arg_list;
	va_start(arg_list,Format);
	vsnprintf(Buffer+Len,sizeof(Buffer)-1-Len,Format,arg_list);
	va_end(arg_list);
	Buffer[sizeof(Buffer)-1] = '\0';
	Send(Buffer);
}

void DALogManager::Ren_Log_Event(const char *Output) {
	char Buffer[526];
	char Time[64];
	Format_Time(Time,sizeof(Time));
	snprintf(Buffer,sizeof(Buffer),"002%s %s",Time,Output);
	Send(Buffer);
}

void DALogManager::Console_Output_Event(const char *Output) {
	char Buffer[526];
	snprintf(Buffer,sizeof(Buffer),"003%s",Output);
	Send(Buffer);
}

void DALogManager::Send(const char *Data) {
	for (int i = 0;i < Connections.Count();i++) {
		Connection *c = Connections[i];
		if (c->Dead) {
			continue;
		}
		c->Pending.append(Data,strlen(Data)+1);
		if (!Flush(c) || c->Pending.size() > MAX_PENDING_BYTES) {
			c->Dead = true;
		}
	}
}

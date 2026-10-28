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
** Server list reporting for the community GameSpy master servers.
**
** Implements the GameSpy v0 query and reporting protocol as documented by Tiberian Technologies for their
** TT 4.8 broadcaster: heartbeats to the masters on UDP 27900, and basic/info/rules/players/status/echo
** queries answered on the query port. See docs/networking.md.
*/

#ifndef _GAMESPY_QNR_H_
#define _GAMESPY_QNR_H_

#include <winsock2.h>
#include <string>
#include <utility>
#include <vector>

class CGameSpyQnR
{
public:
	CGameSpyQnR();
	~CGameSpyQnR();

	/*
	** Opens the query socket once a C&C game is loaded and reporting is enabled.
	*/
	void Init(void);

	/*
	** Tells the masters the server is going away and closes the query socket.
	*/
	void Shutdown(void);

	/*
	** Answers queries and sends heartbeats. Call every frame.
	*/
	void Think(void);

	/*
	** Comma separated "host[:port]" list. Hosts that do not resolve are skipped.
	*/
	bool Parse_HeartBeat_List(const char *list, bool log_on_error = true);

	const char *Get_GameSpy_GameName(void) const { return GameName; }
	const char *Get_Default_HeartBeat_List(void) const { return DefaultHeartBeatList; }

	/*
	** True when no master server resolved, so the server is not listed anywhere.
	*/
	bool IsOffline(void) const { return Masters.empty(); }

	void Enable_Reporting(bool enable) { Enabled = enable; }
	bool IsEnabled(void) const { return Enabled; }

	/*
	** Name of the rules key that identifies OpenW3D servers, and its value for this build. Servers can only
	** be joined by clients with the same value.
	*/
	static const char *Get_Compatibility_Key(void) { return "OpenW3D"; }
	static std::string Get_Compatibility_Value(void);

private:
	typedef std::vector<std::pair<std::string, std::string> > KeyValueList;

	void Send_Heartbeat(int state_changed);
	void Handle_Packet(const char *data, int length, const sockaddr_in &from);
	void Send_Reply(const KeyValueList &pairs, const sockaddr_in &to);

	void Add_Basic(KeyValueList &out);
	void Add_Info(KeyValueList &out);
	void Add_Rules(KeyValueList &out);
	void Add_Players(KeyValueList &out);
	void Load_Custom_Rules(void);

	static const char *GameName;
	static const char *DefaultHeartBeatList;

	SOCKET Socket;
	bool Enabled;
	bool Initialized;
	std::vector<sockaddr_in> Masters;
	unsigned int LastHeartbeatTime;
	unsigned int QueryId;
	int LastHeartbeatResult;

	std::string LastMapName;
	unsigned int MapStartTime;

	KeyValueList CustomRules;
	unsigned int LastCustomRulesLoad;
};

extern CGameSpyQnR GameSpyQnR;

#endif

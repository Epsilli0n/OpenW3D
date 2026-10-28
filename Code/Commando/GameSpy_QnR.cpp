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
** This replaces the GameSpy SDK query/reporting code. The wire format follows the Tiberian Technologies
** broadcaster (TT 4.8 r8841) so OpenW3D servers show up in every lister that reads the community masters:
**
**   - Heartbeat: "\heartbeat\<queryport>\gamename\ccrenegade\final\" to UDP 27900 on each master, at least
**     every 30 seconds. Masters answer "\heartbeatresult\<code>\final\" (0 = listed).
**   - Queries on the query port: basic, info, rules, players, status (all four) and echo.
**   - Replies are "\key\value" pairs. Long replies are split into parts under ~400 characters, each ending
**     in "\queryid\<message>.<part>"; the last part ends in "\final\queryid\<message>.<part>".
*/

#include "GameSpy_QnR.h"
#include <ws2tcpip.h>

#include "specialbuilds.h"
#include "buildnum.h"
#include "cnetwork.h"
#include "combat.h"
#include "ConsoleMode.h"
#include "gamedata.h"
#include "gameinitmgr.h"
#include "GameSpyBanList.h"
#include "gdcnc.h"
#include "ini.h"
#include "netutil.h"
#include "player.h"
#include "playermanager.h"
#include "rawfile.h"
#include "systimer.h"
#include "team.h"
#include "teammanager.h"
#include "useroptions.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

CGameSpyQnR GameSpyQnR;

#if defined(MULTIPLAYERDEMO)
const char *CGameSpyQnR::GameName = "ccrenegadedemo";
#else
const char *CGameSpyQnR::GameName = "ccrenegade";
#endif

/*
** Community masters, in the order TT's broadcaster uses them.
*/
const char *CGameSpyQnR::DefaultHeartBeatList =
	"gamespy.tiberiantechnologies.org:27900, zenmasters.cncirc.net:27900, renmaster.cncirc.net:27900, "
	"renmaster-backup.cncirc.net:27900, renmaster-backup2.cncirc.net:27900, "
	"renmaster-backup3.cncirc.net:27900, renmaster-backup4.cncirc.net:27900";

namespace {

constexpr unsigned int HEARTBEAT_INTERVAL_MS = 25 * 1000;		// TT asks for at most 30 seconds.
constexpr unsigned int CUSTOM_RULES_RELOAD_MS = 60 * 1000;
constexpr unsigned int BANLIST_RELOAD_MS = 60 * 1000;
constexpr size_t MAX_PART_LENGTH = 400;
constexpr const char *CUSTOM_RULES_FILE = "gamespy_rules.ini";
constexpr const char *CUSTOM_RULES_SECTION = "GamespyRules";

/*
** Keys and values cannot contain the separator.
*/
std::string Clean(const char *text)
{
	std::string out = text ? text : "";
	for (char &c : out) {
		if (c == '\\') {
			c = '/';
		}
	}
	const size_t first = out.find_first_not_of(" \t");
	const size_t last = out.find_last_not_of(" \t");
	return (first == std::string::npos) ? std::string() : out.substr(first, last - first + 1);
}

std::string Clean(const WideStringClass &text)
{
	StringClass narrow;
	text.Convert_To(narrow);
	return Clean(narrow.Peek_Buffer());
}

std::string Format_Int(int value)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%d", value);
	return buffer;
}

/*
** TT reports all times as HH.mm.ss, with the hours uncapped.
*/
std::string Format_Time(float seconds)
{
	unsigned int total = (seconds > 0.0f) ? static_cast<unsigned int>(seconds) : 0;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%02u.%02u.%02u", total / 3600, (total / 60) % 60, total % 60);
	return buffer;
}

/*
** "C&C_Islands.mix" -> "C&C". Used as the game type when no mod package is loaded.
*/
std::string Map_Prefix(const char *map_name)
{
	std::string name = map_name ? map_name : "";
	const size_t underscore = name.find('_');
	return (underscore == std::string::npos) ? name : name.substr(0, underscore);
}

std::string Strip_Extension(const char *file_name)
{
	std::string name = file_name ? file_name : "";
	const size_t dot = name.rfind('.');
	return (dot == std::string::npos) ? name : name.substr(0, dot);
}

bool Resolve_Master_With_Timeout(const char *host, WORD port, sockaddr_in &out_addr, std::chrono::milliseconds timeout)
{
	// Fast path for dotted-quad literals so we never touch DNS.
	unsigned long numeric = inet_addr(host);
	if (numeric != INADDR_NONE) {
		out_addr = {};
		out_addr.sin_family = AF_INET;
		out_addr.sin_addr.s_addr = numeric;
		out_addr.sin_port = htons(port);
		return true;
	}

	struct ResolveState {
		std::atomic<bool> done{false};
		bool resolved{false};
		sockaddr_in addr{};
		std::string host;
	};
	auto state = std::make_shared<ResolveState>();
	state->host = host;

	std::thread resolver([state]() {
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;

		addrinfo *info = nullptr;
		if (::getaddrinfo(state->host.c_str(), nullptr, &hints, &info) == 0 && info != nullptr) {
			state->addr = *reinterpret_cast<sockaddr_in *>(info->ai_addr);
			state->resolved = true;
		}
		if (info) {
			::freeaddrinfo(info);
		}
		state->done.store(true, std::memory_order_release);
	});

	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!state->done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	if (!state->done.load(std::memory_order_acquire)) {
		resolver.detach(); // DNS is still in flight; carry on without blocking.
		return false;
	}

	resolver.join();
	if (state->resolved) {
		state->addr.sin_port = htons(port);
		out_addr = state->addr;
		return true;
	}
	return false;
}

} // anonymous namespace


CGameSpyQnR::CGameSpyQnR(void) :
	Socket(INVALID_SOCKET),
	Enabled(false),
	Initialized(false),
	LastHeartbeatTime(0),
	QueryId(0),
	LastHeartbeatResult(-1),
	MapStartTime(0),
	LastCustomRulesLoad(0)
{
}

CGameSpyQnR::~CGameSpyQnR(void)
{
	if (Socket != INVALID_SOCKET) {
		closesocket(Socket);
		Socket = INVALID_SOCKET;
	}
}

std::string CGameSpyQnR::Get_Compatibility_Value(void)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(cNetwork::Get_Exe_Key()));
	return buffer;
}

void CGameSpyQnR::Init(void)
{
#ifndef BETACLIENT
	if (!Enabled || Initialized || !The_Game() || The_Game()->Get_Game_Type() != cGameData::GAME_TYPE_CNC) {
		return;
	}

	ConsoleBox.Print("Initializing server list reporting\n");

	if (Masters.empty()) {
		Parse_HeartBeat_List(DefaultHeartBeatList, false);
	}
	if (Masters.empty()) {
		ConsoleBox.Print("No master servers resolved; the server will not be listed.\n");
	}

	/*
	** Bind the query port on the preferred address, or all of them.
	*/
	extern unsigned int g_ip_override;
	sockaddr_in local = {};
	local.sin_family = AF_INET;
	local.sin_port = htons(static_cast<u_short>(cUserOptions::GameSpyQueryPort.Get()));
	if (g_ip_override != INADDR_NONE) {
		local.sin_addr.s_addr = g_ip_override;
	} else if (cUserOptions::PreferredGameSpyNic.Get()) {
		local.sin_addr.s_addr = cUserOptions::PreferredGameSpyNic.Get();
	} else {
		local.sin_addr.s_addr = INADDR_ANY;
	}

	Socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (Socket == INVALID_SOCKET) {
		ConsoleBox.Print("Server list: could not create the query socket (WinSock %d)\n", WSAGetLastError());
		return;
	}
	if (::bind(Socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) == SOCKET_ERROR) {
		ConsoleBox.Print("Server list: could not bind query port %d (WinSock %d)\n",
			cUserOptions::GameSpyQueryPort.Get(), WSAGetLastError());
		closesocket(Socket);
		Socket = INVALID_SOCKET;
		return;
	}
	u_long non_blocking = 1;
	ioctlsocket(Socket, FIONBIO, &non_blocking);

	ConsoleBox.Print("Server list: query port %d, %d master server(s)\n",
		cUserOptions::GameSpyQueryPort.Get(), static_cast<int>(Masters.size()));

	Initialized = true;
	LastHeartbeatResult = -1;
	LastMapName = The_Game()->Get_Map_Name().Peek_Buffer();
	MapStartTime = TIMEGETTIME();
	Load_Custom_Rules();
	Send_Heartbeat(0);
#endif
}

void CGameSpyQnR::Shutdown(void)
{
	if (Initialized) {
		ConsoleBox.Print("Shutting down server list reporting\n");
		Send_Heartbeat(2);
	}
	if (Socket != INVALID_SOCKET) {
		closesocket(Socket);
		Socket = INVALID_SOCKET;
	}
	Initialized = false;
	Enabled = false;
}

void CGameSpyQnR::Think(void)
{
	static unsigned int ban_reload_time = static_cast<unsigned int>(0 - BANLIST_RELOAD_MS);
	static unsigned int ban_think_time = 0;

	const unsigned int now = TIMEGETTIME();

	if (now - ban_reload_time > BANLIST_RELOAD_MS) {
		GameSpyBanList.LoadBans();
		ban_reload_time = now;
	}
	if (now - ban_think_time > 500) {
		GameSpyBanList.Think();
		ban_think_time = now;
	}

#ifndef BETACLIENT
	if (!Initialized || !Enabled || !GameInitMgrClass::Is_LAN_Initialized() || CombatManager::Is_Loading_Level() ||
			!The_Game()) {
		return;
	}

	/*
	** A new map resets the elapsed time and tells the masters to query again.
	*/
	const char *map_name = The_Game()->Get_Map_Name().Peek_Buffer();
	if (LastMapName != map_name) {
		LastMapName = map_name;
		MapStartTime = now;
		Send_Heartbeat(1);
	}

	if (now - LastHeartbeatTime > HEARTBEAT_INTERVAL_MS) {
		Send_Heartbeat(0);
	}

	if (now - LastCustomRulesLoad > CUSTOM_RULES_RELOAD_MS) {
		Load_Custom_Rules();
	}

	char buffer[1024];
	for (;;) {
		sockaddr_in from = {};
		int from_length = sizeof(from);
		const int received = ::recvfrom(Socket, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr *>(&from), &from_length);
		if (received <= 0) {
			break;
		}
		buffer[received] = 0;
		Handle_Packet(buffer, received, from);
	}
#endif
}

bool CGameSpyQnR::Parse_HeartBeat_List(const char *list, bool log_on_error)
{
	Masters.clear();

	std::string entries = list ? list : "";
	size_t start = 0;
	while (start <= entries.size()) {
		size_t comma = entries.find(',', start);
		if (comma == std::string::npos) {
			comma = entries.size();
		}
		std::string entry = Clean(entries.substr(start, comma - start).c_str());
		start = comma + 1;
		if (entry.empty()) {
			continue;
		}

		WORD port = 27900;
		const size_t colon = entry.find(':');
		if (colon != std::string::npos) {
			const int value = std::atoi(entry.c_str() + colon + 1);
			if (value > 0 && value <= 0xffff) {
				port = static_cast<WORD>(value);
			}
			entry.resize(colon);
		}

		// Bounded DNS lookup so an offline server does not hang at startup.
		sockaddr_in address = {};
		if (Resolve_Master_With_Timeout(entry.c_str(), port, address, std::chrono::milliseconds(1000))) {
			Masters.push_back(address);
		} else if (log_on_error) {
			ConsoleBox.Print("Skipping HeartBeat master '%s' (unresolved)\n", entry.c_str());
		}
	}

	if (Masters.empty() && log_on_error) {
		ConsoleBox.Print("Error processing HeartBeat List: <%s>\n", list ? list : "");
	}
	return !Masters.empty();
}

/*
** state_changed: 0 = regular, 1 = game state changed, 2 = server is exiting.
*/
void CGameSpyQnR::Send_Heartbeat(int state_changed)
{
	if (Socket == INVALID_SOCKET) {
		return;
	}

	char message[128];
	if (state_changed) {
		std::snprintf(message, sizeof(message), "\\heartbeat\\%d\\gamename\\%s\\statechanged\\%d\\final\\",
			cUserOptions::GameSpyQueryPort.Get(), GameName, state_changed);
	} else {
		std::snprintf(message, sizeof(message), "\\heartbeat\\%d\\gamename\\%s\\final\\",
			cUserOptions::GameSpyQueryPort.Get(), GameName);
	}

	for (const sockaddr_in &master : Masters) {
		::sendto(Socket, message, static_cast<int>(std::strlen(message)), 0,
			reinterpret_cast<const sockaddr *>(&master), sizeof(master));
	}
	LastHeartbeatTime = TIMEGETTIME();
}

void CGameSpyQnR::Handle_Packet(const char *data, int length, const sockaddr_in &from)
{
	/*
	** Split "\key\value\key\value..." into tokens. Queries are keys with empty values ("\status\").
	*/
	std::vector<std::string> tokens;
	std::string current;
	for (int i = 0; i < length; ++i) {
		if (data[i] == '\\') {
			tokens.push_back(current);
			current.clear();
		} else {
			current += data[i];
		}
	}
	tokens.push_back(current);

	KeyValueList reply;
	bool answered = false;

	for (size_t i = 1; i < tokens.size(); i += 2) {
		const std::string &key = tokens[i];
		const std::string value = (i + 1 < tokens.size()) ? tokens[i + 1] : std::string();

		if (key == "heartbeatresult") {
			const int code = std::atoi(value.c_str());
			if (code != LastHeartbeatResult) {
				ConsoleBox.Print("Server list: master %s replied heartbeat result %d%s\n",
					cNetUtil::Address_To_String(from.sin_addr.s_addr), code, code == 0 ? " (listed)" : "");
				LastHeartbeatResult = code;
			}
			return;
		} else if (key == "basic") {
			Add_Basic(reply);
			answered = true;
		} else if (key == "info") {
			Add_Info(reply);
			answered = true;
		} else if (key == "rules") {
			Add_Rules(reply);
			answered = true;
		} else if (key == "players") {
			Add_Players(reply);
			answered = true;
		} else if (key == "status") {
			Add_Basic(reply);
			Add_Info(reply);
			Add_Rules(reply);
			Add_Players(reply);
			answered = true;
		} else if (key == "echo") {
			reply.emplace_back("echo", Clean(value.c_str()));
			answered = true;
		}
	}

	if (answered) {
		Send_Reply(reply, from);
	}
}

void CGameSpyQnR::Send_Reply(const KeyValueList &pairs, const sockaddr_in &to)
{
	++QueryId;
	int part = 1;
	std::string packet;

	auto send_part = [&](bool final_part) {
		char suffix[48];
		std::snprintf(suffix, sizeof(suffix), "%s\\queryid\\%u.%d", final_part ? "\\final" : "", QueryId, part);
		packet += suffix;
		::sendto(Socket, packet.c_str(), static_cast<int>(packet.size()), 0,
			reinterpret_cast<const sockaddr *>(&to), sizeof(to));
		packet.clear();
		++part;
	};

	for (const auto &pair : pairs) {
		const std::string item = "\\" + pair.first + "\\" + pair.second;
		if (!packet.empty() && packet.size() + item.size() > MAX_PART_LENGTH) {
			send_part(false);
		}
		packet += item;
	}
	send_part(true);
}

void CGameSpyQnR::Add_Basic(KeyValueList &out)
{
	out.emplace_back("gamename", GameName);
	out.emplace_back("gamever", Format_Int(static_cast<int>(BuildInfoClass::Get_Network_Version_Number())));
}

void CGameSpyQnR::Add_Info(KeyValueList &out)
{
	cGameData *game = The_Game();

	out.emplace_back("hostname", Clean(WideStringClass(game->Get_Game_Title())));
	out.emplace_back("hostport", Format_Int(game->Get_Port()));
	out.emplace_back("mapname", Clean(game->Get_Map_Name().Peek_Buffer()));
	out.emplace_back("nextmap", Clean(game->Get_Next_Map_Name().Peek_Buffer()));

	/*
	** Game type is the mod package name, or the map prefix when there is none.
	*/
	if (!game->Get_Mod_Name().Is_Empty()) {
		out.emplace_back("gametype", Clean(Strip_Extension(game->Get_Mod_Name().Peek_Buffer()).c_str()));
	} else {
		out.emplace_back("gametype", Clean(Map_Prefix(game->Get_Map_Name().Peek_Buffer()).c_str()));
	}

	int player_count = 0;
	for (SLNode<cPlayer> *node = cPlayerManager::Get_Player_Object_List()->Head(); node; node = node->Next()) {
		if (node->Data()->Get_Is_Active().Is_True()) {
			++player_count;
		}
	}
	out.emplace_back("numplayers", Format_Int(player_count));
	out.emplace_back("maxplayers", Format_Int(game->Get_Max_Players()));

	const float time_limit = game->Is_Time_Limit() ? game->Get_Time_Limit_Minutes() * 60.0f : 0.0f;
	const float elapsed = (TIMEGETTIME() - MapStartTime) / 1000.0f;
	out.emplace_back("timeleft", Format_Time(game->Is_Time_Limit() ? game->Get_Time_Remaining_Seconds() : 0.0f));
	out.emplace_back("timeelapsed", Format_Time(elapsed));
	out.emplace_back("timelimit", Format_Time(time_limit));
}

void CGameSpyQnR::Add_Rules(KeyValueList &out)
{
	cGameData *game = The_Game();
	const char *zero = "0";
	const char *one = "1";

	out.emplace_back("SVER", std::string("OpenW3D/") + BuildInfoClass::Get_Build_Version_String());
	out.emplace_back("CSVR", ConsoleBox.Is_Exclusive() ? one : zero);
	out.emplace_back("DED", game->IsDedicated.Get() ? one : zero);
	out.emplace_back("DG", game->DriverIsAlwaysGunner.Get() ? one : zero);
	out.emplace_back("password", game->IsPassworded.Get() ? one : zero);
	out.emplace_back("TC", game->IsTeamChangingAllowed.Get() ? one : zero);
	out.emplace_back("FF", game->IsFriendlyFirePermitted.Get() ? one : zero);

	if (game->Get_Game_Type() == cGameData::GAME_TYPE_CNC) {
		cGameDataCnc *cnc = game->As_Cnc();
		out.emplace_back("SC", Format_Int(cnc->Get_Starting_Credits()));
		out.emplace_back("BDEG", cnc->BaseDestructionEndsGame.Get() ? one : zero);
		out.emplace_back("BPEG", cnc->BeaconPlacementEndsGame.Get() ? one : zero);
	}

	/*
	** OpenW3D clients only list servers they can join: same key as their own build.
	*/
	out.emplace_back(Get_Compatibility_Key(), Get_Compatibility_Value());

	for (const auto &rule : CustomRules) {
		out.push_back(rule);
	}
}

void CGameSpyQnR::Add_Players(KeyValueList &out)
{
	for (SLNode<cTeam> *node = cTeamManager::Get_Team_Object_List()->Head(); node; node = node->Next()) {
		cTeam *team = node->Data();
		const std::string id = "_t" + Format_Int(team->Get_Id());
		out.emplace_back("team" + id, Clean(team->Get_Name()));
		out.emplace_back("kills" + id, Format_Int(team->Get_Kills()));
		out.emplace_back("deaths" + id, Format_Int(team->Get_Deaths()));
		out.emplace_back("score" + id, Format_Int(static_cast<int>(team->Get_Score())));
	}

	for (SLNode<cPlayer> *node = cPlayerManager::Get_Player_Object_List()->Head(); node; node = node->Next()) {
		cPlayer *player = node->Data();
		if (player->Get_Is_Active().Is_False()) {
			continue;
		}
		const std::string id = "_" + Format_Int(player->Get_Id());
		out.emplace_back("player" + id, Clean(player->Get_Name()));
		out.emplace_back("kills" + id, Format_Int(player->Get_Kills()));
		out.emplace_back("deaths" + id, Format_Int(player->Get_Deaths()));
		out.emplace_back("score" + id, Format_Int(static_cast<int>(player->Get_Score())));
		out.emplace_back("team" + id, Format_Int(player->Get_Player_Type()));
		out.emplace_back("ping" + id, Format_Int(player->Get_Ping()));
		out.emplace_back("time" + id, Format_Time(player->Get_Game_Time()));
	}
}

/*
** Extra rules from gamespy_rules.ini in the server folder, as in TT:
**
**   [GamespyRules]
**   Owner=Someone
**   Discord=discord.gg/abc
*/
void CGameSpyQnR::Load_Custom_Rules(void)
{
	LastCustomRulesLoad = TIMEGETTIME();
	CustomRules.clear();

	RawFileClass file(CUSTOM_RULES_FILE);
	if (!file.Is_Available()) {
		return;
	}

	INIClass ini(file);
	const int count = ini.Entry_Count(CUSTOM_RULES_SECTION);
	for (int i = 0; i < count; ++i) {
		const char *key = ini.Get_Entry(CUSTOM_RULES_SECTION, i);
		if (!key) {
			continue;
		}
		char value[256] = {};
		ini.Get_String(CUSTOM_RULES_SECTION, key, "", value, sizeof(value));
		const std::string clean_key = Clean(key);
		if (!clean_key.empty() && clean_key != Get_Compatibility_Key()) {
			CustomRules.emplace_back(clean_key, Clean(value));
		}
	}
}

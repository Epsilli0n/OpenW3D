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

#include "serverbrowser.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <cstdlib>
#include <cstring>

ServerBrowserClass ServerBrowser;

namespace {

constexpr unsigned short MASTER_LIST_PORT = 28900;
constexpr int MASTER_CONNECT_TIMEOUT_MS = 4000;
constexpr int MASTER_READ_TIMEOUT_MS = 6000;
constexpr int QUERY_TIMEOUT_MS = 3000;

/*
** The list request used by GameSpy v0 listers. enctype 0 asks for the plain 6 byte per server format.
*/
constexpr const char *LIST_REQUEST =
	"\\gamename\\ccrenegade\\enctype\\0\\validate\\TVJFLK6J\\final\\"
	"\\queryid\\1.1\\list\\cmp\\gamename\\ccrenegade\\where\\\\final\\";

typedef std::chrono::steady_clock Clock;

int Elapsed_Ms(Clock::time_point since)
{
	return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

bool Resolve(const std::string &host, unsigned short port, sockaddr_in &out)
{
	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *info = nullptr;
	if (::getaddrinfo(host.c_str(), nullptr, &hints, &info) != 0 || info == nullptr) {
		return false;
	}
	out = *reinterpret_cast<sockaddr_in *>(info->ai_addr);
	out.sin_port = htons(port);
	::freeaddrinfo(info);
	return true;
}

bool Wait_Socket(SOCKET s, bool for_write, int timeout_ms)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(s, &set);
	timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	const int result = for_write ? ::select(0, nullptr, &set, nullptr, &tv) : ::select(0, &set, nullptr, nullptr, &tv);
	return result > 0;
}

int To_Int(const std::map<std::string, std::string> &values, const char *key)
{
	auto it = values.find(key);
	return (it == values.end()) ? 0 : std::atoi(it->second.c_str());
}

std::string To_String(const std::map<std::string, std::string> &values, const char *key)
{
	auto it = values.find(key);
	return (it == values.end()) ? std::string() : it->second;
}

} // anonymous namespace


ServerBrowserClass::ServerBrowserClass() :
	State(STATE_IDLE),
	CancelRequested(false),
	ListedCount(0)
{
}

ServerBrowserClass::~ServerBrowserClass()
{
	Cancel();
	if (Worker.joinable()) {
		Worker.join();
	}
}

void ServerBrowserClass::Refresh(const char *master_list, const std::string &compat_value)
{
	const StateType state = State.load();
	if (state == STATE_FETCHING_LIST || state == STATE_QUERYING) {
		return;
	}
	if (Worker.joinable()) {
		Worker.join();
	}

	{
		std::lock_guard<std::mutex> guard(Lock);
		Servers.clear();
	}
	ListedCount = 0;
	CancelRequested = false;
	State = STATE_FETCHING_LIST;
	Worker = std::thread(&ServerBrowserClass::Run, this, std::string(master_list ? master_list : ""), compat_value);
}

void ServerBrowserClass::Cancel(void)
{
	CancelRequested = true;
}

std::vector<ServerBrowserEntry> ServerBrowserClass::Get_Servers(void) const
{
	std::lock_guard<std::mutex> guard(Lock);
	return Servers;
}

void ServerBrowserClass::Parse_Key_Values(const char *data, size_t length, std::map<std::string, std::string> &out)
{
	std::vector<std::string> tokens;
	std::string current;
	for (size_t i = 0; i < length; ++i) {
		if (data[i] == '\\') {
			tokens.push_back(current);
			current.clear();
		} else {
			current += data[i];
		}
	}
	tokens.push_back(current);

	/*
	** Token 0 is whatever came before the first backslash. "final" has no value of its own: TT writes
	** "\final\queryid\N.P", the old SDK "\final\\queryid\N.P".
	*/
	size_t i = 1;
	while (i < tokens.size()) {
		const std::string &key = tokens[i];
		if (key == "final") {
			i += (i + 1 < tokens.size() && tokens[i + 1].empty()) ? 2 : 1;
			continue;
		}
		if (key.empty()) {
			++i;
			continue;
		}
		const std::string value = (i + 1 < tokens.size()) ? tokens[i + 1] : std::string();
		if (key != "queryid") {
			out[key] = value;
		}
		i += 2;
	}
}

void ServerBrowserClass::Run(std::string master_list, std::string compat_value)
{
	std::vector<std::pair<unsigned int, unsigned short> > listed;
	if (!Fetch_List(master_list, listed)) {
		State = CancelRequested ? STATE_IDLE : STATE_FAILED;
		return;
	}
	ListedCount = static_cast<int>(listed.size());

	State = STATE_QUERYING;
	Query_Servers(listed, compat_value);
	State = CancelRequested ? STATE_IDLE : STATE_DONE;
}

/*
** Tries each master in turn until one returns a list.
*/
bool ServerBrowserClass::Fetch_List(const std::string &master_list, std::vector<std::pair<unsigned int, unsigned short> > &out)
{
	size_t start = 0;
	while (start <= master_list.size() && !CancelRequested) {
		size_t comma = master_list.find(',', start);
		if (comma == std::string::npos) {
			comma = master_list.size();
		}
		std::string host = master_list.substr(start, comma - start);
		start = comma + 1;

		const size_t first = host.find_first_not_of(" \t");
		const size_t last = host.find_last_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		host = host.substr(first, last - first + 1);

		unsigned short port = MASTER_LIST_PORT;
		const size_t colon = host.find(':');
		if (colon != std::string::npos) {
			const int value = std::atoi(host.c_str() + colon + 1);
			if (value > 0 && value <= 0xffff) {
				port = static_cast<unsigned short>(value);
			}
			host.resize(colon);
		}

		sockaddr_in address = {};
		if (!Resolve(host, port, address)) {
			continue;
		}

		SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET) {
			continue;
		}
		u_long non_blocking = 1;
		ioctlsocket(s, FIONBIO, &non_blocking);
		::connect(s, reinterpret_cast<sockaddr *>(&address), sizeof(address));

		std::string data;
		bool ok = false;
		if (Wait_Socket(s, true, MASTER_CONNECT_TIMEOUT_MS)) {
			const Clock::time_point began = Clock::now();
			char buffer[4096];

			/*
			** The master greets with "\basic\\secure\<challenge>", where the challenge is 6 characters. Read
			** exactly that before asking for the list, so the binary list never mixes with the greeting.
			** enctype 0 does not need an answer to the challenge.
			*/
			std::string greeting;
			bool greeted = false;
			while (!CancelRequested && Elapsed_Ms(began) < MASTER_READ_TIMEOUT_MS) {
				if (!Wait_Socket(s, false, 200)) {
					continue;
				}
				const int received = ::recv(s, buffer, sizeof(buffer), 0);
				if (received <= 0) {
					break;
				}
				greeting.append(buffer, received);
				const size_t secure = greeting.find("\\secure\\");
				if (secure != std::string::npos && greeting.size() >= secure + 8 + 6) {
					greeted = true;
					break;
				}
			}

			if (greeted) {
				::send(s, LIST_REQUEST, static_cast<int>(std::strlen(LIST_REQUEST)), 0);

				/*
				** 6 bytes per server, then "\final\". Server bytes can contain backslashes, so only the
				** trailing "\final\" marks the end.
				*/
				while (!CancelRequested && Elapsed_Ms(began) < MASTER_READ_TIMEOUT_MS) {
					if (!Wait_Socket(s, false, 200)) {
						continue;
					}
					const int received = ::recv(s, buffer, sizeof(buffer), 0);
					if (received <= 0) {
						break;
					}
					data.append(buffer, received);
					if (data.size() >= 7 && data.compare(data.size() - 7, 7, "\\final\\") == 0) {
						ok = true;
						break;
					}
				}
			}
		}
		closesocket(s);

		if (!ok) {
			continue;
		}
		data.resize(data.size() - 7);

		for (size_t i = 0; i + 6 <= data.size(); i += 6) {
			const unsigned char *b = reinterpret_cast<const unsigned char *>(data.data() + i);
			unsigned int ip = 0;
			std::memcpy(&ip, b, 4);	// already network byte order
			const unsigned short query_port = static_cast<unsigned short>((b[4] << 8) | b[5]);
			if (ip != 0 && query_port != 0) {
				out.emplace_back(ip, query_port);
			}
		}
		return true;
	}
	return false;
}

/*
** Sends \status\ to every listed server at once and collects the replies for a few seconds.
*/
void ServerBrowserClass::Query_Servers(const std::vector<std::pair<unsigned int, unsigned short> > &servers, const std::string &compat_value)
{
	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		return;
	}
	u_long non_blocking = 1;
	ioctlsocket(s, FIONBIO, &non_blocking);

	struct Pending {
		Clock::time_point Sent;
		int PingMs = -1;
		bool Done = false;
		std::map<std::string, std::string> Values;
	};
	std::map<unsigned long long, Pending> pending;

	auto key_for = [](unsigned int ip, unsigned short port) {
		return (static_cast<unsigned long long>(ip) << 16) | port;
	};

	for (const auto &server : servers) {
		sockaddr_in to = {};
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = server.first;
		to.sin_port = htons(server.second);
		Pending &entry = pending[key_for(server.first, server.second)];
		entry.Sent = Clock::now();
		::sendto(s, "\\status\\", 8, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
	}

	const Clock::time_point began = Clock::now();
	size_t done = 0;
	char buffer[2048];
	while (!CancelRequested && done < pending.size() && Elapsed_Ms(began) < QUERY_TIMEOUT_MS) {
		if (!Wait_Socket(s, false, 100)) {
			continue;
		}
		sockaddr_in from = {};
		int from_length = sizeof(from);
		const int received = ::recvfrom(s, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &from_length);
		if (received <= 0) {
			continue;
		}

		auto it = pending.find(key_for(from.sin_addr.s_addr, ntohs(from.sin_port)));
		if (it == pending.end() || it->second.Done) {
			continue;
		}
		Pending &entry = it->second;
		if (entry.PingMs < 0) {
			entry.PingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - entry.Sent).count());
		}
		Parse_Key_Values(buffer, received, entry.Values);

		const std::string text(buffer, received);
		if (text.find("\\final\\") == std::string::npos) {
			continue;
		}
		entry.Done = true;
		++done;

		/*
		** Only servers running a matching OpenW3D build can be joined.
		*/
		if (To_String(entry.Values, "OpenW3D") != compat_value) {
			continue;
		}

		ServerBrowserEntry server;
		server.Address = from.sin_addr.s_addr;
		server.QueryPort = ntohs(from.sin_port);
		server.GamePort = static_cast<unsigned short>(To_Int(entry.Values, "hostport"));
		server.PingMs = entry.PingMs;
		server.Players = To_Int(entry.Values, "numplayers");
		server.MaxPlayers = To_Int(entry.Values, "maxplayers");
		server.Passworded = To_Int(entry.Values, "password") != 0;
		server.Name = To_String(entry.Values, "hostname");
		server.Map = To_String(entry.Values, "mapname");
		server.GameType = To_String(entry.Values, "gametype");
		server.Values = entry.Values;
		if (server.GamePort == 0) {
			continue;
		}

		std::lock_guard<std::mutex> guard(Lock);
		Servers.push_back(server);
	}

	closesocket(s);
}

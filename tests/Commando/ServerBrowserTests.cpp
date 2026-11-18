/*
**	Command & Conquer Renegade(tm)
**	Copyright 2026 OpenW3D Contributors.
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
// Tests the internet server list against a fake master and fake servers on loopback.
// Pass --live to fetch the real community list instead (needs internet; not run by ctest).
#include "serverbrowser.h"

#include <winsock2.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace
{
int Failures = 0;

void Check(bool condition, const char *what)
{
	if (!condition) {
		std::printf("FAILED: %s\n", what);
		++Failures;
	}
}

SOCKET Bind_Udp(unsigned short port)
{
	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in local = {};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	local.sin_port = htons(port);
	::bind(s, reinterpret_cast<sockaddr *>(&local), sizeof(local));
	return s;
}

void Test_Parse()
{
	std::map<std::string, std::string> values;
	const char tt[] = "\\hostname\\Test\\mapname\\C&C_Field.mix\\final\\queryid\\12.1";
	ServerBrowserClass::Parse_Key_Values(tt, sizeof(tt) - 1, values);
	Check(values["hostname"] == "Test", "parse hostname");
	Check(values["mapname"] == "C&C_Field.mix", "parse mapname");
	Check(values.count("final") == 0 && values.count("queryid") == 0, "TT framing keys dropped");

	values.clear();
	const char sdk[] = "\\numplayers\\3\\final\\\\queryid\\1.1";
	ServerBrowserClass::Parse_Key_Values(sdk, sizeof(sdk) - 1, values);
	Check(values["numplayers"] == "3", "parse SDK framing");
	Check(values.size() == 1, "SDK framing keys dropped");
}

/*
** A master on loopback that lists two servers, and two servers: one OpenW3D, one TT. The OpenW3D query
** port is 0x5C5C so the list contains backslash bytes.
*/
void Test_Refresh()
{
	const unsigned short openw3d_port = 0x5C5C;
	const unsigned short tt_port = 23700;

	SOCKET master = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in master_addr = {};
	master_addr.sin_family = AF_INET;
	master_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	master_addr.sin_port = 0;
	::bind(master, reinterpret_cast<sockaddr *>(&master_addr), sizeof(master_addr));
	int length = sizeof(master_addr);
	::getsockname(master, reinterpret_cast<sockaddr *>(&master_addr), &length);
	::listen(master, 1);

	SOCKET openw3d = Bind_Udp(openw3d_port);
	SOCKET tt = Bind_Udp(tt_port);
	std::atomic<bool> stop(false);

	std::thread master_thread([&]() {
		SOCKET client = ::accept(master, nullptr, nullptr);
		if (client == INVALID_SOCKET) {
			return;
		}
		const char greeting[] = "\\basic\\\\secure\\IGNORE";
		::send(client, greeting, sizeof(greeting) - 1, 0);
		char request[512];
		::recv(client, request, sizeof(request), 0);

		std::string list;
		const unsigned char loopback[4] = { 127, 0, 0, 1 };
		list.append(reinterpret_cast<const char *>(loopback), 4);
		list += static_cast<char>(openw3d_port >> 8);
		list += static_cast<char>(openw3d_port & 0xff);
		list.append(reinterpret_cast<const char *>(loopback), 4);
		list += static_cast<char>(tt_port >> 8);
		list += static_cast<char>(tt_port & 0xff);
		list += "\\final\\";
		::send(client, list.data(), static_cast<int>(list.size()), 0);
		closesocket(client);
	});

	auto serve = [&](SOCKET s, bool is_openw3d) {
		u_long non_blocking = 1;
		ioctlsocket(s, FIONBIO, &non_blocking);
		while (!stop) {
			char buffer[256];
			sockaddr_in from = {};
			int from_length = sizeof(from);
			if (::recvfrom(s, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &from_length) <= 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}
			std::string part1 = is_openw3d ? "\\hostname\\OpenW3D Test\\hostport\\4848\\mapname\\C&C_Field.mix\\numplayers\\2\\queryid\\1.1"
				: "\\hostname\\TT Server\\hostport\\4848\\mapname\\C&C_Mesa.mix\\queryid\\1.1";
			std::string part2 = is_openw3d ? "\\maxplayers\\16\\password\\1\\OpenW3D\\ABCD1234\\final\\queryid\\1.2"
				: "\\SVER\\TT/4.80-r9000\\final\\queryid\\1.2";
			::sendto(s, part1.data(), static_cast<int>(part1.size()), 0, reinterpret_cast<sockaddr *>(&from), from_length);
			::sendto(s, part2.data(), static_cast<int>(part2.size()), 0, reinterpret_cast<sockaddr *>(&from), from_length);
		}
	};
	std::thread openw3d_thread(serve, openw3d, true);
	std::thread tt_thread(serve, tt, false);

	ServerBrowserClass browser;
	char master_list[64];
	std::snprintf(master_list, sizeof(master_list), "127.0.0.1:%u", ntohs(master_addr.sin_port));
	browser.Refresh(master_list, "ABCD1234");

	const auto began = std::chrono::steady_clock::now();
	while (browser.Get_State() != ServerBrowserClass::STATE_DONE && browser.Get_State() != ServerBrowserClass::STATE_FAILED &&
			std::chrono::steady_clock::now() - began < std::chrono::seconds(10)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	Check(browser.Get_State() == ServerBrowserClass::STATE_DONE, "refresh finished");
	Check(browser.Get_Listed_Count() == 2, "master listed both servers");
	const std::vector<ServerBrowserEntry> servers = browser.Get_Servers();
	Check(servers.size() == 1, "only the OpenW3D server is kept");
	if (!servers.empty()) {
		Check(servers[0].Name == "OpenW3D Test", "server name");
		Check(servers[0].GamePort == 4848, "game port");
		Check(servers[0].QueryPort == openw3d_port, "query port with backslash bytes");
		Check(servers[0].Players == 2 && servers[0].MaxPlayers == 16, "player counts");
		Check(servers[0].Passworded, "password flag");
	}

	stop = true;
	master_thread.join();
	openw3d_thread.join();
	tt_thread.join();
	closesocket(master);
	closesocket(openw3d);
	closesocket(tt);
}

void Run_Live()
{
	ServerBrowserClass browser;
	browser.Refresh("gamespy.tiberiantechnologies.org, renmaster.cncirc.net", "none");
	const auto began = std::chrono::steady_clock::now();
	while (browser.Get_State() != ServerBrowserClass::STATE_DONE && browser.Get_State() != ServerBrowserClass::STATE_FAILED &&
			std::chrono::steady_clock::now() - began < std::chrono::seconds(20)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	std::printf("live: state %d, master lists %d servers, %d joinable\n", browser.Get_State(),
		browser.Get_Listed_Count(), static_cast<int>(browser.Get_Servers().size()));
}
} // namespace

int main(int argc, char **argv)
{
	WSADATA wsa;
	::WSAStartup(MAKEWORD(2, 2), &wsa);

	if (argc > 1 && std::strcmp(argv[1], "--live") == 0) {
		Run_Live();
		::WSACleanup();
		return 0;
	}

	Test_Parse();
	Test_Refresh();

	::WSACleanup();
	if (Failures) {
		std::printf("%d server browser test(s) failed\n", Failures);
		return 1;
	}
	std::printf("Server browser tests passed\n");
	return 0;
}

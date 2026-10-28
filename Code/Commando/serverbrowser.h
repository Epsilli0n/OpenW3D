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
** Internet server list for the client.
**
** Fetches the ccrenegade server list from the community GameSpy masters (TCP 28900), queries each server
** with \status\ (UDP), and keeps the servers this build can join: those whose OpenW3D rule matches our own
** exe key. TT servers never send that rule, so they are left out. Runs on a worker thread; poll Get_State
** and Get_Servers from the UI. See docs/networking.md.
*/

#ifndef SERVERBROWSER_H
#define SERVERBROWSER_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ServerBrowserEntry
{
	unsigned int Address = 0;		// IPv4, network byte order
	unsigned short GamePort = 0;	// hostport, host byte order
	unsigned short QueryPort = 0;	// host byte order
	int PingMs = 0;
	int Players = 0;
	int MaxPlayers = 0;
	bool Passworded = false;
	std::string Name;
	std::string Map;
	std::string GameType;
	std::map<std::string, std::string> Values;	// every key the server reported
};

class ServerBrowserClass
{
public:
	enum StateType {
		STATE_IDLE,
		STATE_FETCHING_LIST,
		STATE_QUERYING,
		STATE_DONE,
		STATE_FAILED,
	};

	ServerBrowserClass();
	~ServerBrowserClass();

	/*
	** Starts a new refresh unless one is running. master_list is a comma separated host[:port] list of
	** masters to try in order (port defaults to 28900); compat_value is our own OpenW3D rule value.
	*/
	void Refresh(const char *master_list, const std::string &compat_value);
	void Cancel(void);

	StateType Get_State(void) const { return State.load(); }
	int Get_Listed_Count(void) const { return ListedCount.load(); }	// servers the master knows about
	std::vector<ServerBrowserEntry> Get_Servers(void) const;		// joinable servers found so far

	/*
	** Parses "\key\value..." into pairs. Exposed for tests.
	*/
	static void Parse_Key_Values(const char *data, size_t length, std::map<std::string, std::string> &out);

private:
	void Run(std::string master_list, std::string compat_value);
	bool Fetch_List(const std::string &master_list, std::vector<std::pair<unsigned int, unsigned short> > &out);
	void Query_Servers(const std::vector<std::pair<unsigned int, unsigned short> > &servers, const std::string &compat_value);

	std::thread Worker;
	std::atomic<StateType> State;
	std::atomic<bool> CancelRequested;
	std::atomic<int> ListedCount;

	mutable std::mutex Lock;
	std::vector<ServerBrowserEntry> Servers;
};

extern ServerBrowserClass ServerBrowser;

#endif // SERVERBROWSER_H

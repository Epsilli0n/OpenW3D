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

/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Commando/ServerSettings.cpp                  $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 8/09/02 2:12p                                               $*
 *                                                                                             *
 *                    $Revision:: 17                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "ServerSettings.h"
#include "wwdebug.h"
#include <limits>
#include "gamedata.h"
#include "gdcnc.h"
#include "ini.h"
#include "registry.h"
#include "rawfile.h"
#include "ConsoleMode.h"
#include "specialbuilds.h"
#include "_globals.h"
#include "bandwidth.h"
#include "useroptions.h"
#include "gamespyadmin.h"
#include "servercontrol.h"
#include "gamesideservercontrol.h"
#include "AutoStart.h"
#include "GameSpy_QnR.h"
#include "bandwidthcheck.h"
#include <cstdio>
#include <winsock2.h>



const char *ConfigSettingsName = "Config";
const char *MasterServerSection = "Server";
const char *GameSettingsSection = "Game";

char ServerSettingsClass::SettingsFile[MAX_PATH];
bool ServerSettingsClass::IsActive = false;
char ServerSettingsClass::MasterPassword[128];
ServerSettingsClass::GameModeTypeEnum ServerSettingsClass::GameMode = MODE_NONE;
unsigned int ServerSettingsClass::MasterBandwidth = 0;
int ServerSettingsClass::DiskLogSize = -1;






/***********************************************************************************************
 * ServerSettingsClass::Set_Settings_File_Name -- Set the name of the settings file            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    File name                                                                         *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/1/2002 12:07PM ST : Created                                                             *
 *=============================================================================================*/
void ServerSettingsClass::Set_Settings_File_Name(char *filename)
{
	if (strlen(filename) < sizeof(SettingsFile)) {
		strcpy(SettingsFile, filename);
		IsActive = true;
	}
}



/***********************************************************************************************
 * ServerSettingsClass::Parse -- Pull the server info out of the settings file                 *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    True if we should apply the settings. False to parse for errors only.             *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/1/2002 12:07PM ST : Created                                                             *
 *=============================================================================================*/
bool ServerSettingsClass::Parse(bool apply)
{
	char master_settings[MAX_PATH];
	int master_bw;
	char remote_admin_pass[128];
	char game_type[32];
	char heartbeat_list[512];
	char remote_admin_ip[128];

	MasterPassword[0] = 0;

	/*
	** IsActive is set when the settigns file name is set.
	*/
	if (IsActive) {

		WWASSERT(The_Game() || !apply);
		if (apply) {
			WWDEBUG_SAY(("Applying server settings\n"));
			ConsoleBox.Print("Applying server settings\n");
		}

		/*
		** Make sure the server config file is there. It should be since it's verified at command line parsing time.
		*/
		RawFileClass file(SettingsFile);
		if (!file.Is_Available()) {
			WWDEBUG_SAY(("Server startup file '%s' not found\n", SettingsFile));
			ConsoleBox.Print("Error - server startup file '%s' not found - aborting\n", SettingsFile);
			ConsoleBox.Wait_For_Keypress();
			return(false);
		}

		/*
		** Get the name of the master server settings file.
		*/
		INIClass ini(file);
		ini.Get_String(MasterServerSection, ConfigSettingsName, "", master_settings, sizeof(master_settings));

		/*
		** Load the master server settings from the ini file.
		*/


		/*
		** Game Type.
		*/
		ini.Get_String(MasterServerSection, "GameType", "LAN", game_type, sizeof(game_type));
		if (cGameSpyAdmin::Get_Is_Server_Gamespy_Listed()) {
			strcpy(game_type, "GameSpy");
		}
		if (stricmp(game_type, "WOL") == 0) {
			/*
			** Westwood Online is gone. Stock server.ini files still ask for it, so host on the LAN instead of
			** sitting idle without a game port.
			*/
			ConsoleBox.Print("Warning - Westwood Online is no longer available. Hosting with GameType = LAN instead\n");
			strcpy(game_type, "LAN");
		}
		if (stricmp(game_type, "LAN") == 0) {
			GameMode = MODE_LAN;
		} else if (stricmp(game_type, "GameSpy") == 0) {
			cGameSpyAdmin::Set_Is_Server_Gamespy_Listed(true);
			GameSpyQnR.Enable_Reporting(true);
			GameMode = MODE_GAMESPY;
		} else {
			WWDEBUG_SAY(("Bad game type specified in server.ini\n"));
			ConsoleBox.Print("Error - Unknown game type in server settings. Use 'LAN' or 'GameSpy' - aborting\n");
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}

		/*
		** Parse the list of GameSpy Style Master servers from the HeartBeat List
	*/
	heartbeat_list[sizeof(heartbeat_list)-1] = 0;
	ini.Get_String(MasterServerSection, "HeartBeatServers", GameSpyQnR.Get_Default_HeartBeat_List(), heartbeat_list, sizeof(heartbeat_list)-1);
	GameSpyQnR.Parse_HeartBeat_List(heartbeat_list, false);

		/*
		** Game settings live in the [Game] section of this file. Older setups keep them in a separate
		** data\svrcfg_*.ini named by Config; that still works.
		*/
		if (ini.Section_Present(GameSettingsSection)) {
			cGameData::Use_Server_Config_File(SettingsFile, GameSettingsSection);
			if (!Check_Game_Settings_File(SettingsFile)) {
				WWDEBUG_SAY(("Game settings in '%s' not usable\n", SettingsFile));
				ConsoleBox.Print("Error - the [%s] section of '%s' contains errors - aborting\n", GameSettingsSection, SettingsFile);
				ConsoleBox.Wait_For_Keypress();
				return(false);
			}
			if (apply && The_Game()) {
				The_Game()->Set_Ini_Filename(SettingsFile);
			}
		} else {
			if (strlen(master_settings) == 0) {
				strcpy(master_settings, "svrcfg_cnc.ini");
			}
			if (apply) {
				ConsoleBox.Print("Reading game settings from data\\%s. Move them into a [%s] section of %s to keep everything in one file.\n",
					master_settings, GameSettingsSection, SettingsFile);
			}
		}

		/*
		** Make sure the master server settings file is there.
		*/
		char filename[MAX_PATH];
		sprintf(filename, "data\\%s", master_settings);
		file.Set_Name(filename);
		if (ini.Section_Present(GameSettingsSection)) {
			/*
			** Already checked above.
			*/
		} else if (!file.Is_Available()) {
			WWDEBUG_SAY(("Server settings file '%s' not found\n", filename));
			ConsoleBox.Print("Error - server settings file '%s' not found - aborting\n", filename);
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		} else {
			if (!Check_Game_Settings_File(master_settings)) {
				WWDEBUG_SAY(("Server settings file '%s' not usable\n", master_settings));
				ConsoleBox.Print("Error - server settings file '%s' contains errors - aborting\n", master_settings);
				ConsoleBox.Wait_For_Keypress();;
				return(false);
			}
			if (apply && The_Game()) {
				The_Game()->Set_Ini_Filename(master_settings);
			}
		}

		/*
		** Restart Flag
		*/

		RegistryClass restart_reg(APPLICATION_SUB_KEY_NAME_WOLSETTINGS);
		if (restart_reg.Is_Valid ()) {
			restart_reg.Set_Int(AutoRestartClass::REG_VALUE_AUTO_RESTART_FLAG, 1);
			switch (GameMode) {
				case MODE_LAN:
				case MODE_GAMESPY:
					restart_reg.Set_Int(AutoRestartClass::REG_VALUE_AUTO_RESTART_TYPE, 0);
					break;
				case MODE_NONE:
					break;
			}
		}

		/*
		** Get the GameSpy Query port number.
		*/
		int gsqport = cUserOptions::GameSpyQueryPort.Get();
		gsqport = ini.Get_Int(MasterServerSection, "GameSpyQueryPort", gsqport);
		if (!gsqport) gsqport = cUserOptions::GameSpyQueryPort.Get();
		if (gsqport < 0 || gsqport > 0xffff) {
			WWDEBUG_SAY(("Error - Invalid port number %d specified for GameSpy Query Port - aborting\n", gsqport));
			ConsoleBox.Print("Error - Invalid port number %d specified for GameSpy Query Port - aborting\n", gsqport);
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}
		cUserOptions::GameSpyQueryPort.Set(gsqport);

		/*
		** Get the GameSpy Game port number.
		*/
		int gsgport = cUserOptions::GameSpyGamePort.Get();
		gsgport = ini.Get_Int(MasterServerSection, "GameSpyGamePort", gsgport);
		if (!gsgport) gsgport = cUserOptions::GameSpyGamePort.Get();
		if (gsgport < 0 || gsgport > 0xffff || gsgport == gsqport) {
			WWDEBUG_SAY(("Error - Invalid port number %d specified for GameSpy Game Port - aborting\n", gsgport));
			ConsoleBox.Print("Error - Invalid port number %d specified for GameSpy Game Port - aborting\n", gsgport);
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}
		cUserOptions::GameSpyGamePort.Set(gsgport);
		if (cGameSpyAdmin::Get_Is_Server_Gamespy_Listed()) {
			ConsoleBox.Print("GameSpy ports: game=%d query=%d\n", gsgport, gsqport);
		}

		/*
		** Get the bandwidth allowance.
		*/
		master_bw = ini.Get_Int(MasterServerSection, "BandwidthUp", 0xffffffff);
		if (master_bw != 0 && master_bw != 0xffffffff && master_bw < 33600) {
			WWDEBUG_SAY(("Error - Insufficient bandwidth specified for master server - aborting\n"));
			ConsoleBox.Print("Error - Insufficient bandwidth specified for master server - aborting\n");
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}
		MasterBandwidth = master_bw;

		/*
		** Get the max size of the disk log in days.
		*/
		DiskLogSize = ini.Get_Int(MasterServerSection, "DiskLogSize", 7);
		if (DiskLogSize > 365) {
			WWDEBUG_SAY(("Error - Disk log size too large - aborting\n"));
			ConsoleBox.Print("Error - Disk log size too large - aborting\n");
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}

		/*
		** Get the Network Update Rate override.
		*/
		int nur = ini.Get_Int(MasterServerSection, "NetUpdateRate", 8);
		if (nur < 5 || nur > 30) {
			WWDEBUG_SAY(("Error - Bad NetUpdateRate specified - aborting\n"));
			ConsoleBox.Print("Error - NetUpdateRate must be between 5 and 30 - aborting\n");
			ConsoleBox.Wait_For_Keypress();;
			return(false);
		}
		cUserOptions::NetUpdateRate.Set(nur);

		/*
		** Get the remote admin settings.
		*/
		bool allow_remote = ini.Get_Bool(MasterServerSection, "AllowRemoteAdmin", false);
		RegistryClass reg_remote(APPLICATION_SUB_KEY_NAME_NET_SERVER_CONTROL);
		if (allow_remote) {
			ini.Get_String(MasterServerSection, "RemoteAdminPassword", "", remote_admin_pass, sizeof(remote_admin_pass));
			const size_t len = ::strlen(remote_admin_pass);
			if (len == 0) {
				ConsoleBox.Print("Error - Remote admin password must be specified - aborting\n");
				ConsoleBox.Wait_For_Keypress();;
				return(false);
			} else {
				if (len > 31) {
					ConsoleBox.Print("Error - Remote admin password too long - aborting\n");
					ConsoleBox.Wait_For_Keypress();;
					return(false);
				}
			}
			int admin_port = ini.Get_Int(MasterServerSection, "RemoteAdminPort", 0);
			if (admin_port != 0 && (admin_port < 1024 || admin_port > 65535-8)) {
				ConsoleBox.Print("Error - Remote admin port number out of range - aborting\n");
				ConsoleBox.Wait_For_Keypress();;
				return(false);
			}

			ServerControl.Allow_Remote_Admin(true);

			/*
			** Set the port number into the registry.
			*/
			if (admin_port == 0) {
				admin_port = DEFAULT_SERVER_CONTROL_PORT;
			}
			reg_remote.Set_Int(SERVER_CONTROL_PORT_KEY, admin_port);

			/*
			** Set the password into the registry.
			*/
			reg_remote.Set_String(SERVER_CONTROL_PASSWORD_KEY, remote_admin_pass);

			/*
			** We need to bind to more than just the loopback address when listening for control messages.
			*/
			reg_remote.Set_Int(SERVER_CONTROL_LOOPBACK_KEY, 0);

			/*
			** There may be an IP override specified.
			*/
			ini.Get_String(MasterServerSection, "RemoteAdminIP", "0.0.0.0", remote_admin_ip, sizeof(remote_admin_ip));
			unsigned int admin_ip_long = ntohl(inet_addr(remote_admin_ip));
			reg_remote.Set_Int(SERVER_CONTROL_IP_KEY, admin_ip_long);

		} else {

			/*
			** Only listen to control messages on the loopback address.
			*/
			reg_remote.Set_Int(SERVER_CONTROL_LOOPBACK_KEY, 1);
			ServerControl.Allow_Remote_Admin(false);
		}


		/*
		** Set the master settings into the registry.
		*/
		//if (apply) {

			/*
			** Bandwidth.
			*/
			if (master_bw != 0xffffffff) {
				RegistryClass reg_netopt(APPLICATION_SUB_KEY_NAME_NETOPTIONS);
				if (reg_netopt.Is_Valid()) {
					if (cGameSpyAdmin::Is_Gamespy_Game()) {
						if (master_bw == 0) master_bw = 1000000;
						cUserOptions::Set_Bandwidth_Bps(master_bw);
					} else {
						cUserOptions::Set_Bandwidth_Type(BANDWIDTH_LANT1);
					}
				}
			}
		//}
	}
	return(true);
}



/*
** Commented default server file. Everything the dedicated server reads lives here: network and listing
** settings in [Server], game rules and the map rotation in [Game], extra server list rules in [ListingRules].
*/
static const char DefaultSettingsFile[] =
	"; OpenW3D dedicated server settings. Everything the server reads is in this one file.\r\n"
	"; Changes to [Game] are picked up when the current map ends.\r\n"
	"\r\n"
	"[Server]\r\n"
	"; LAN hosts on the local network only. GameSpy also lists the server on the community master servers.\r\n"
	"GameType = LAN\r\n"
	"; Game port (UDP). 0 uses 4848.\r\n"
	"GameSpyGamePort = 0\r\n"
	"; Server list query port (UDP). 0 uses 25300. Forward both ports to list the server from behind a router.\r\n"
	"GameSpyQueryPort = 0\r\n"
	"; Master servers to report to, comma separated host[:port]. Leave out to use the community masters.\r\n"
	"; HeartBeatServers =\r\n"
	"; Upstream bandwidth in bits per second. 0 means unlimited.\r\n"
	"BandwidthUp = 0\r\n"
	"; Network updates per second sent to each client (5-30).\r\n"
	"NetUpdateRate = 8\r\n"
	"; Days of console log files to keep.\r\n"
	"DiskLogSize = 7\r\n"
	"; Remote administration (RenRem protocol). Without it the server only listens on the local machine.\r\n"
	"AllowRemoteAdmin = false\r\n"
	"RemoteAdminPassword =\r\n"
	"RemoteAdminIP =\r\n"
	"RemoteAdminPort =\r\n"
	"\r\n"
	"[Game]\r\n"
	"bGameTitle = OpenW3D Server\r\n"
	"bMotd =\r\n"
	"IsPassworded = no\r\n"
	"bPassword =\r\n"
	"MaxPlayers = 16\r\n"
	"TimeLimitMinutes = 30\r\n"
	"StartingCredits = 0\r\n"
	"; 0 none, 1 team, 2 all\r\n"
	"RadarMode = 1\r\n"
	"IsFriendlyFirePermitted = no\r\n"
	"IsTeamChangingAllowed = no\r\n"
	"RemixTeams = yes\r\n"
	"CanRepairBuildings = yes\r\n"
	"DriverIsAlwaysGunner = yes\r\n"
	"SpawnWeapons = no\r\n"
	"; no lets the game run with players on one team only (gameplay is otherwise pending)\r\n"
	"RequireOpponent = yes\r\n"
	"BaseDestructionEndsGame = yes\r\n"
	"BeaconPlacementEndsGame = yes\r\n"
	"UseLagReduction = yes\r\n"
	"IsAutoRestart = yes\r\n"
	"; Map rotation, MapName00 to MapName99. MapName is the first map.\r\n"
	"DoMapsLoop = yes\r\n"
	"MapName = C&C_Field.mix\r\n"
	"MapName00 = C&C_Field.mix\r\n"
	"MapName01 = C&C_Volcano.mix\r\n"
	"MapName02 = C&C_Under.mix\r\n"
	"MapName03 = C&C_Walls.mix\r\n"
	"MapName04 = C&C_City.mix\r\n"
	"MapName05 = C&C_Canyon.mix\r\n"
	"MapName06 = C&C_Hourglass.mix\r\n"
	"MapName07 = C&C_Mesa.mix\r\n"
	"MapName08 = C&C_Islands.mix\r\n"
	"MapName09 = C&C_Complex.mix\r\n"
	"\r\n"
	"[ListingRules]\r\n"
	"; Extra key = value pairs shown in server lists, for example:\r\n"
	"; Owner = Someone\r\n"
	"; Discord = discord.gg/example\r\n";

bool ServerSettingsClass::Write_Default_Settings_File(const char *filename)
{
	RawFileClass file(filename);
	if (file.Is_Available()) {
		return(false);
	}
	if (!file.Open(RawFileClass::WRITE)) {
		return(false);
	}
	file.Write(DefaultSettingsFile, sizeof(DefaultSettingsFile) - 1);
	file.Close();
	return(true);
}


/***********************************************************************************************
 * ServerSettingsClass::Check_Game_Settings_File -- Check game settings for validity           *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Game settings .ini file name                                                      *
 *                                                                                             *
 * OUTPUT:   True if valid.                                                                    *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/3/2002 9:37PM ST : Created                                                              *
 *=============================================================================================*/
bool ServerSettingsClass::Check_Game_Settings_File(char *config_file)
{
	cGameDataCnc *game_settings = (cGameDataCnc*) cGameData::Create_Game_Of_Type(cGameData::GAME_TYPE_CNC);
	WWASSERT(game_settings != nullptr);

	if (game_settings) {
		game_settings->Set_Ini_Filename(config_file);
		game_settings->Load_From_Server_Config();

		WideStringClass outMsg;
		bool ok = game_settings->Is_Valid_Settings(outMsg, true);

		delete game_settings;
		return(ok);
	}
	return(false);
}

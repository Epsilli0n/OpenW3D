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
		if (strlen(master_settings) == 0) {
			if (apply) {
				WWDEBUG_SAY(("No master server settings specified - using defaults\n"));
				ConsoleBox.Print("No master server settings specified - using defaults\n");
			}
		}

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
		** Make sure the master server settings file is there.
		*/
		char filename[MAX_PATH];
		sprintf(filename, "data\\%s", master_settings);
		file.Set_Name(filename);
		if (!file.Is_Available()) {
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

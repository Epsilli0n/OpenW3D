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
 *                     $Archive:: /Commando/Code/Commando/AutoStart.cpp                       $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 8/13/02 4:58p                                               $*
 *                                                                                             *
 *                    $Revision:: 24                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"
#include "AutoStart.h"
#include "gameinitmgr.h"
#include "registry.h"
#include "_globals.h"
#include "gamedata.h"
#include "campaign.h"
#include "win.h"
#include "Except.h"
#include "listctrl.h"
#include "DlgWOLAutoStart.h"
#include "slavemaster.h"
#include "gamesideservercontrol.h"
#include "gamemode.h"
#include "ConsoleMode.h"
#include "ServerSettings.h"
#include "renegadedialogmgr.h"
#include "openw3d.h"
#include "gamespyadmin.h"
#include "specialbuilds.h"
#include <vector>

/*
** Single instance of restart class.
*/
AutoRestartClass AutoRestart;

/*
** Registry entries.
*/
const char *AutoRestartClass::REG_VALUE_AUTO_RESTART_FLAG = "AutoRestartFlag";
const char *AutoRestartClass::REG_VALUE_AUTO_RESTART_TYPE = "AutoRestartType";

/*
** Stupid extern for main loop exit.
*/
extern void Stop_Main_Loop (int exitCode);


AutoRestartProgressDialogClass *AutoRestartProgressDialogClass::Instance;


/***********************************************************************************************
 * AutoRestartClass::AutoRestartClass -- Class constructor                                     *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 3:21PM ST : Created                                                             *
 *=============================================================================================*/
AutoRestartClass::AutoRestartClass(void)
{
	RestartState = STATE_DONE;
	CancelRequest = false;
}


/***********************************************************************************************
 * AutoRestartClass::Restart_Game -- Initiate the restart process                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 3:22PM ST : Created                                                             *
 *=============================================================================================*/
void AutoRestartClass::Restart_Game(void)
{
	if (RestartState == STATE_DONE) {
		RestartState = STATE_FIRST;
		CancelRequest = false;
		Set_Restart_Flag(false);
	}
}


/***********************************************************************************************
 * AutoRestartClass::Cancel -- Cancel game restart                                             *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 9:42PM ST : Created                                                             *
 *=============================================================================================*/
void AutoRestartClass::Cancel(void)
{
	CancelRequest = true;
}


/***********************************************************************************************
 * AutoRestartClass::Think -- Class service function                                           *
 *                                                                                             *
 * Servers always host on the LAN game mode. Internet listing (GameType = GameSpy) is the LAN  *
 * mode plus heartbeats to the community master server.                                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 3:22PM ST : Created                                                             *
 *=============================================================================================*/
void AutoRestartClass::Think(void)
{
	bool can_render = ConsoleBox.Is_Exclusive() ? false : true;

	switch (RestartState) {

		/*
		** Idle, not doing anything.
		*/
		case STATE_DONE:
			break;

		/*
		** This is the first restart state. Initialise the LAN game mode.
		*/
		case STATE_FIRST:

			if (CancelRequest) {
				RestartState = STATE_CANCELLED;
				break;
			}

			/*
			** Create the progress dialog.
			*/
			if (can_render) {
				START_DIALOG(AutoRestartProgressDialogClass);
				AutoRestartProgressDialogClass::Get_Instance()->Add_Text(U_CHAR("Auto starting game. Type 'quit' at the console window to abort"));
			}
			ConsoleBox.Print("*** Auto starting game. Type 'quit' to abort ***\n");

			/*
			** Start listening for server control messages.
			*/
			GameSideServerControlClass::Init();

			if (can_render) {
				AutoRestartProgressDialogClass::Get_Instance()->Add_Text(U_CHAR("Initializing LAN Mode"));
			}
			if (cGameSpyAdmin::Get_Is_Server_Gamespy_Listed()) {
				ConsoleBox.Print("Initializing GameSpy Mode\n");
			} else {
				ConsoleBox.Print("Initializing LAN Mode\n");
			}
			GameInitMgrClass::Initialize_LAN();
			RestartState = STATE_GAME_MODE_WAIT;
			break;


		case STATE_GAME_MODE_WAIT:
		{
			if (cGameData::Is_Manual_Exit()) {
				RestartState = STATE_DONE;
				Stop_Main_Loop (EXIT_SUCCESS);
				break;
			}

			GameModeClass *game_mode = GameModeManager::Find("LAN");
			if (game_mode && game_mode->Is_Active()) {
				RestartState = STATE_CREATE_GAME;
				break;
			}

			/*
			** It won't have initialzed above if a shutdown was already pending. Maybe we need to try again.
			*/
			if (game_mode && game_mode->Is_Inactive()) {
				GameInitMgrClass::Initialize_LAN();
			}
			break;
		}


		/*
		** Create the game object and initialise it with the server settings.
		*/
		case STATE_CREATE_GAME:
		{
			if (cGameData::Is_Manual_Exit()) {
				RestartState = STATE_DONE;
				Stop_Main_Loop (EXIT_SUCCESS);
				break;
			}

			if (CancelRequest) {
				RestartState = STATE_CANCELLED;
				break;
			}

			if (can_render) {
				AutoRestartProgressDialogClass::Get_Instance()->Add_Text(U_CHAR("Building game info"));
			}

			/*
			** Create the new game data.
			*/
			PTheGameData = cGameData::Create_Game_Of_Type(cGameData::GAME_TYPE_CNC);
			WWASSERT(PTheGameData != nullptr);

			/*
			** Apply command line/ini settings.
			*/
			if (ServerSettingsClass::Parse(true) == false) {
				RestartState = STATE_DONE;
				Stop_Main_Loop (EXIT_SUCCESS);
				break;
			}

			/*
			** Load alternate server settings if required.
			*/
			if (SlaveMaster.Am_I_Slave()) {
				RegistryClass reg(APPLICATION_SUB_KEY_NAME_OPTIONS);
				char file_name[MAX_PATH];
				reg.Get_String("MultiplayerSettings", file_name, sizeof(file_name), "");
				if (strlen(file_name)) {
					WWDEBUG_SAY(("Loading multiplayer settings from file %s\n", file_name));
					The_Game()->Set_Ini_Filename(file_name);
				}
			}

			/*
			** Load the game settings.
			*/
			The_Game()->Load_From_Server_Config();

			/*
			** Override game settings in command line mode.
			*/
			if (ServerSettingsClass::Is_Command_Line_Mode()) {
				The_Game()->IsAutoRestart.Set(true);
				The_Game()->IsDedicated.Set(true);
			}

			StringClass inifile = The_Game()->Get_Ini_Filename();
			StringClass nick(32, true);
			cNetInterface::Get_Nickname().Convert_To(nick);
			ConsoleBox.Set_Title(nick.Peek_Buffer(), inifile.Peek_Buffer());

			RestartState = STATE_START_GAME;
			break;
		}


		/*
		** Load the game.
		*/
		case STATE_START_GAME:
		{
			if (cGameData::Is_Manual_Exit()) {
				RestartState = STATE_DONE;
				Stop_Main_Loop (EXIT_SUCCESS);
				break;
			}

			SlaveMaster.Startup_Slaves();

			WWASSERT(PTheGameData != nullptr);
			CampaignManager::Select_Backdrop_Number_By_MP_Type(The_Game()->Get_Game_Type());
			GameInitMgrClass::Set_Is_Client_Required(false);
			GameInitMgrClass::Set_Is_Server_Required(true);
			if (can_render) {
				AutoRestartProgressDialogClass::Get_Instance()->End_Dialog();
			}
			GameInitMgrClass::Start_Game(The_Game()->Get_Map_Name(), -1, 0);
			RestartState = STATE_DONE;
			break;
		}

		/*
		** User cancelled.
		*/
		case STATE_CANCELLED:
			RestartState = STATE_DONE;

			if (can_render && AutoRestartProgressDialogClass::Get_Instance()) {
				AutoRestartProgressDialogClass::Get_Instance()->End_Dialog();
			}
			if (SlaveMaster.Am_I_Slave()) {
				Stop_Main_Loop (EXIT_SUCCESS);
			} else {
				RenegadeDialogMgrClass::Goto_Location (RenegadeDialogMgrClass::LOC_MAIN_MENU);
			}
			break;

	}
}


/***********************************************************************************************
 * AutoRestartClass::Set_Restart_Flag -- Set state of auto restart mode                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 3:32PM ST : Created                                                             *
 *=============================================================================================*/
void AutoRestartClass::Set_Restart_Flag(bool enable)
{
	RegistryClass registry (APPLICATION_SUB_KEY_NAME_WOLSETTINGS);
	if (registry.Is_Valid ()) {
		registry.Set_Int(REG_VALUE_AUTO_RESTART_FLAG, enable ? 1 : 0);

		if (enable) {
			/*
			** Type 0 is LAN. Older builds used 1 for Westwood Online.
			*/
			registry.Set_Int(REG_VALUE_AUTO_RESTART_TYPE, 0);
			Set_Exit_On_Exception(true);
		} else {
			Set_Exit_On_Exception(false);
		}

		{

			if (enable) {
				const char *renegade_exe = Process::GetCurrentProcessPath();
				if (!renegade_exe) {
					WWRELEASE_ERROR(("Failed to get path to executable, cannot restart\n"));
					return;
				}
				std::vector<const char *> restart_args;

				restart_args.push_back(renegade_exe);
				restart_args.push_back("--ini");
				restart_args.push_back(OpenW3D::Get_Config_File_Path());

				StringClass start_server_arg;
				if (ServerSettingsClass::Is_Active()) {
					start_server_arg.Format("--startserver=%s", ServerSettingsClass::Get_Settings_File_Name());
					restart_args.push_back(start_server_arg.Peek_Buffer());
				}

				if (ConsoleBox.Is_Exclusive()) {
					restart_args.push_back("--nodx");
				}
				restart_args.push_back(nullptr);

				WWDEBUG_SAY(("Registering app to restart not implemented\n"));
			} else {
				WWDEBUG_SAY(("Unregistering app from restaring not implemented\n"));
			}
		}
	}
}


/***********************************************************************************************
 * AutoRestartClass::Get_Restart_Flag -- Is a restart indicated by the registry?               *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/5/2001 3:32PM ST : Created                                                             *
 *=============================================================================================*/
bool AutoRestartClass::Get_Restart_Flag(void)
{
	bool flag = false;

	RegistryClass registry (APPLICATION_SUB_KEY_NAME_WOLSETTINGS);
	if (registry.Is_Valid()) {
		int restart = registry.Get_Int(REG_VALUE_AUTO_RESTART_FLAG, 0);
		flag = restart ? true : false;
	}
	return(flag);
}

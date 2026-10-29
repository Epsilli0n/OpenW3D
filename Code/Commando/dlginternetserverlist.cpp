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
** Internet server list dialog. See dlginternetserverlist.h.
**
** Joining does what cGameSpyAdmin::Connect_To_Game_Server does for --connect, but directly instead of through
** cGameSpyAdmin::Think: Think waits for SplashIntroMenuDialogClass::Is_Complete(), which is only set when the
** game was started in GameSpy mode, and its bandwidth check and Join_Server quit the game on failure, cancel,
** or disconnect (IsLaunchedFromGamespy), which is right for a launcher but not for an in-game menu.
*/

#include "dlginternetserverlist.h"
#include "win.h"
#include "listctrl.h"
#include "editctrl.h"
#include "gamedata.h"
#include "gameinitmgr.h"
#include "cnetwork.h"
#include "netinterface.h"
#include "gamespyadmin.h"
#include "GameSpy_QnR.h"
#include "DlgMPConnect.h"
#include "DlgPasswordPrompt.h"
#include "renegadedialog.h"
#include "renegadedialogmgr.h"
#include "translatedb.h"
#include "string_ids.h"
#include "specialbuilds.h"
#include <algorithm>
#include <cctype>


////////////////////////////////////////////////////////////////
//	Local constants
////////////////////////////////////////////////////////////////
enum
{
	COL_SERVER_NAME	= 0,
	COL_MAP,
	COL_PLAYERS,
	COL_PING,
	COL_MAX
};


////////////////////////////////////////////////////////////////
//
//	InternetServerListMenuClass
//
////////////////////////////////////////////////////////////////
InternetServerListMenuClass::InternetServerListMenuClass (void)	:
	MenuDialogClass (GetRenegadeDialog(RenegadeDialogID::IDD_MP_INTERNET_SERVER_LIST)),
	LastState (ServerBrowserClass::STATE_IDLE),
	LastListedCount (-1),
	RefreshPending (false)
{
	WWDEBUG_SAY(("InternetServerListMenu instantiated\n"));

	//
	//	This is the internet entry point now (it used to be the GameSpy menu), so mark the client as being in
	// the internet menus while it is open. That picks the internet nickname and connection settings.
	//
	cGameSpyAdmin::Set_Is_Under_Gamespy_Menuing (true);
}


////////////////////////////////////////////////////////////////
//
//	~InternetServerListMenuClass
//
////////////////////////////////////////////////////////////////
InternetServerListMenuClass::~InternetServerListMenuClass (void)
{
	cGameSpyAdmin::Set_Is_Under_Gamespy_Menuing (false);
	WWDEBUG_SAY(("InternetServerListMenu destroyed\n"));
}


////////////////////////////////////////////////////////////////
//
//	On_Last_Menu_Ending
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_Last_Menu_Ending (void)
{
	RenegadeDialogMgrClass::Goto_Location (RenegadeDialogMgrClass::LOC_MAIN_MENU);
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_Init_Dialog
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_Init_Dialog (void)
{
#ifdef FREEDEDICATEDSERVER
	Get_Dlg_Item(IDC_JOIN_GAME_BUTTON)->Enable(false);
#endif // FREEDEDICATEDSERVER

	//
	//	Configure the columns
	//
	ListCtrlClass *list_ctrl = (ListCtrlClass *)Get_Dlg_Item (IDC_GAME_LIST_CTRL);
	if (list_ctrl != nullptr) {
		WideStringClass col_name;

		col_name = TRANSLATE (IDS_MP_GAME_LIST_HEADER_GAME_NAME);
		list_ctrl->Add_Column (col_name, 0.45F, Vector3 (1, 1, 1));

		col_name = TRANSLATE (IDS_MP_GAME_LIST_HEADER_GAME_MAP);
		list_ctrl->Add_Column (col_name, 0.25F, Vector3 (1, 1, 1));

		col_name = TRANSLATE (IDS_MP_GAME_LIST_HEADER_PLAYERS);
		list_ctrl->Add_Column (col_name, 0.15F, Vector3 (1, 1, 1));

		col_name = TRANSLATE (IDS_MP_GAME_LIST_HEADER_PING);
		list_ctrl->Add_Column (col_name, 0.15F, Vector3 (1, 1, 1));
	}

	//
	//	Put the nickname into the nickname edit control
	//
	EditCtrlClass *name_edit = (EditCtrlClass *)Get_Dlg_Item (IDC_NICKNAME_EDIT);
	WWASSERT(name_edit != nullptr);

	if (name_edit != nullptr) {
		name_edit->Set_Text_Limit (9);
		name_edit->Set_Text (cNetInterface::Get_Nickname ());

		bool enable = (name_edit->Get_Text_Length () > 0);
		Enable_Dlg_Item (IDC_JOIN_GAME_BUTTON, enable);
	}

	MenuDialogClass::On_Init_Dialog ();

	Start_Refresh ();
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_Destroy
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_Destroy (void)
{
	ServerBrowser.Cancel ();

	//
	//	Store the nickname
	//
	WideStringClass nickname = Get_Dlg_Item_Text (IDC_NICKNAME_EDIT);
	cNetInterface::Set_Nickname (nickname);
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_Command
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_Command (int ctrl_id, int message_id, unsigned int param)
{
	switch (ctrl_id) {

		case IDC_REFRESH_GAME_LIST:
			Start_Refresh ();
			break;

		case IDC_JOIN_GAME_BUTTON:
			Join_Game ();
			break;
	}

	MenuDialogClass::On_Command (ctrl_id, message_id, param);
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_Key_Down
//
////////////////////////////////////////////////////////////////
bool
InternetServerListMenuClass::On_Key_Down (uint32 key_id, uint32 key_data)
{
	if (VK_F5 == key_id) {
		Start_Refresh ();
		return true;
	}

	return MenuDialogClass::On_Key_Down (key_id, key_data);
}


////////////////////////////////////////////////////////////////
//
//	On_Frame_Update
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_Frame_Update (void)
{
	const ServerBrowserClass::StateType state = ServerBrowser.Get_State ();

	//
	//	A refresh asked for while an older (cancelled) one was still winding down was ignored by the
	// browser; try again once it has stopped.
	//
	if (state == ServerBrowserClass::STATE_IDLE && RefreshPending) {
		Start_Refresh ();
	} else {
		const int listed_count = ServerBrowser.Get_Listed_Count ();
		if (state != LastState || listed_count != LastListedCount) {

			//
			//	Only rebuild the list when a refresh finishes (or fails), not every frame
			//
			if (state != LastState) {
				if (state == ServerBrowserClass::STATE_DONE) {
					RefreshPending = false;
					Update_Server_List ();
				} else if (state == ServerBrowserClass::STATE_FAILED) {
					RefreshPending = false;
					Servers.clear ();
					ListCtrlClass *list_ctrl = (ListCtrlClass *)Get_Dlg_Item (IDC_GAME_LIST_CTRL);
					if (list_ctrl != nullptr) {
						list_ctrl->Delete_All_Entries ();
					}
				}
			}

			LastState			= state;
			LastListedCount	= listed_count;
			Update_Status ();
		}
	}

	MenuDialogClass::On_Frame_Update ();
	return ;
}


////////////////////////////////////////////////////////////////
//
//	Start_Refresh
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Start_Refresh (void)
{
	//
	//	The browser ignores this while a refresh is already running
	//
	//
	//	OPENW3D_MASTER_SERVERS (comma separated host[:port], TCP list port) overrides the community masters,
	// for private masters and for testing.
	//
	const char *override_list = ::getenv ("OPENW3D_MASTER_SERVERS");
	const std::string master_list = (override_list != nullptr && override_list[0] != '\0')
		? std::string (override_list)
		: Build_Master_List (GameSpyQnR.Get_Default_HeartBeat_List ());
	ServerBrowser.Refresh (master_list.c_str (), CGameSpyQnR::Get_Compatibility_Value ());
	RefreshPending = true;

	//
	//	Make the next frame treat whatever state the browser is in as new, so a refresh that finishes
	// before we look again still rebuilds the list
	//
	LastState = ServerBrowserClass::STATE_IDLE;
	return ;
}


////////////////////////////////////////////////////////////////
//
//	Build_Master_List
//
//	The heartbeat list names the masters' UDP heartbeat ports (27900). The browser wants the TCP list port,
// which it defaults to (28900), so keep only the host names: "a:27900, b:27900" becomes "a,b".
//
////////////////////////////////////////////////////////////////
std::string
InternetServerListMenuClass::Build_Master_List (const char *heartbeat_list)
{
	std::string result;
	if (heartbeat_list == nullptr) {
		return result;
	}

	std::string entry;
	for (const char *ch = heartbeat_list; ; ++ch) {
		if (*ch == ',' || *ch == '\0') {

			//
			//	Trim the entry and drop any ":port"
			//
			const size_t colon = entry.find (':');
			if (colon != std::string::npos) {
				entry.erase (colon);
			}
			const size_t first = entry.find_first_not_of (" \t");
			const size_t last = entry.find_last_not_of (" \t");
			if (first != std::string::npos) {
				if (!result.empty ()) {
					result += ',';
				}
				result += entry.substr (first, last - first + 1);
			}
			entry.clear ();

			if (*ch == '\0') {
				break;
			}
		} else {
			entry += *ch;
		}
	}

	return result;
}


////////////////////////////////////////////////////////////////
//
//	Set_Status_Text
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Set_Status_Text (const WideStringClass &text)
{
	if (StatusText.Compare (text) != 0) {
		StatusText = text;
		Set_Dlg_Item_Text (IDC_SERVER_LIST_STATUS, StatusText);
	}

	return ;
}


////////////////////////////////////////////////////////////////
//
//	Update_Status
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Update_Status (void)
{
	WideStringClass text;

	switch (LastState) {

		case ServerBrowserClass::STATE_FETCHING_LIST:
			text = U_CHAR("Contacting master server...");
			break;

		case ServerBrowserClass::STATE_QUERYING:
			text.Format (U_CHAR("Querying %d servers..."), LastListedCount);
			break;

		case ServerBrowserClass::STATE_DONE:
			if (Servers.empty ()) {
				text.Format (U_CHAR("No OpenW3D servers found (%d listed)"), LastListedCount);
			} else if (Servers.size () == 1) {
				text.Format (U_CHAR("1 OpenW3D server found (%d listed)"), LastListedCount);
			} else {
				text.Format (U_CHAR("%d OpenW3D servers found (%d listed)"), (int)Servers.size (), LastListedCount);
			}
			break;

		case ServerBrowserClass::STATE_FAILED:
			text = U_CHAR("Could not reach the master servers");
			break;

		default:
			//
			//	Idle (cancelled, e.g. after a join attempt): keep the last message
			//
			return ;
	}

	Set_Status_Text (text);
	return ;
}


////////////////////////////////////////////////////////////////
//
//	Update_Server_List
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Update_Server_List (void)
{
	ListCtrlClass *list_ctrl = (ListCtrlClass *)Get_Dlg_Item (IDC_GAME_LIST_CTRL);
	if (list_ctrl == nullptr) {
		return ;
	}

	//
	//	Remember the selected server so we can select it again
	//
	bool had_selection = false;
	unsigned int selected_address = 0;
	unsigned short selected_port = 0;
	int curr_sel = list_ctrl->Get_Curr_Sel ();
	if (curr_sel != -1) {
		size_t index = (size_t)list_ctrl->Get_Entry_Data (curr_sel, 0);
		if (index < Servers.size ()) {
			had_selection		= true;
			selected_address	= Servers[index].Address;
			selected_port		= Servers[index].GamePort;
		}
	}

	//
	//	Take the new results, fastest first
	//
	Servers = ServerBrowser.Get_Servers ();
	std::stable_sort (Servers.begin (), Servers.end (),
		[](const ServerBrowserEntry &a, const ServerBrowserEntry &b) { return a.PingMs < b.PingMs; });

	list_ctrl->Delete_All_Entries ();

	bool found_selected = false;
	for (size_t index = 0; index < Servers.size (); index ++) {
		const ServerBrowserEntry &entry = Servers[index];

		int item_index = list_ctrl->Insert_Entry ((int)index, U_CHAR(""));
		if (item_index < 0) {
			continue;
		}

		//
		//	Server name, marked when it needs a password
		//
		WideStringClass name_string;
		name_string.Convert_From (entry.Name.c_str ());
		if (entry.Passworded) {
			name_string = U_CHAR("* ") + name_string;
		}

		//
		//	Map name without the .mix extension
		//
		std::string map_name = entry.Map;
		if (map_name.size () >= 4) {
			std::string ext = map_name.substr (map_name.size () - 4);
			std::transform (ext.begin (), ext.end (), ext.begin (), [](unsigned char c) { return (char)std::tolower (c); });
			if (ext == ".mix") {
				map_name.erase (map_name.size () - 4);
			}
		}
		WideStringClass map_string;
		map_string.Convert_From (map_name.c_str ());

		WideStringClass players_string;
		players_string.Format (U_CHAR("%d/%d"), entry.Players, entry.MaxPlayers);

		WideStringClass ping_string;
		ping_string.Format (U_CHAR("%d"), entry.PingMs);

		list_ctrl->Set_Entry_Text (item_index, COL_SERVER_NAME,	name_string);
		list_ctrl->Set_Entry_Text (item_index, COL_MAP,			map_string);
		list_ctrl->Set_Entry_Text (item_index, COL_PLAYERS,		players_string);
		list_ctrl->Set_Entry_Text (item_index, COL_PING,			ping_string);

		//
		//	The entry data is the index into Servers
		//
		list_ctrl->Set_Entry_Data (item_index, 0, (uintptr_t)index);

		if (had_selection && !found_selected &&
			 entry.Address == selected_address && entry.GamePort == selected_port) {
			list_ctrl->Set_Curr_Sel (item_index);
			found_selected = true;
		}
	}

	//
	//	Select the first entry by default
	//
	if (found_selected == false && list_ctrl->Get_Entry_Count () > 0) {
		list_ctrl->Set_Curr_Sel (0);
	}

	return ;
}


////////////////////////////////////////////////////////////////
//
//	Join_Game
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Join_Game (void)
{
	if (!Is_Dlg_Item_Enabled (IDC_JOIN_GAME_BUTTON)) {
		return ;
	}

	ListCtrlClass *list_ctrl = (ListCtrlClass *)Get_Dlg_Item (IDC_GAME_LIST_CTRL);
	if (list_ctrl == nullptr) {
		return ;
	}

	int curr_sel = list_ctrl->Get_Curr_Sel ();
	if (curr_sel < 0) {
		return ;
	}

	size_t index = (size_t)list_ctrl->Get_Entry_Data (curr_sel, 0);
	if (index >= Servers.size ()) {
		return ;
	}

	const ServerBrowserEntry &entry = Servers[index];
	if (entry.Address == 0 || entry.GamePort == 0) {
		return ;
	}

	//
	//	Set the new nickname
	//
	JoinNickname = Get_Dlg_Item_Text (IDC_NICKNAME_EDIT);
	cNetInterface::Set_Nickname (JoinNickname);

	PendingJoin = entry;

	//
	//	Passworded servers: connect once the prompt signals us with the password
	//
	if (PendingJoin.Passworded) {
		DlgPasswordPrompt::DoDialog (this);
	} else {
		WideStringClass no_password;
		cGameSpyAdmin::Set_Password_Attempt (no_password);
		Connect_To_Server ();
	}

	return ;
}


////////////////////////////////////////////////////////////////
//
// Handle receipt of password entered signal from the password
// prompt dialog.
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::ReceiveSignal (DlgPasswordPrompt &password_dialog)
{
	WideStringClass wide_password = password_dialog.GetPassword ();
	cGameSpyAdmin::Set_Password_Attempt (wide_password);
	Connect_To_Server ();
	return ;
}


////////////////////////////////////////////////////////////////
//
//	Connect_To_Server
//
//	Same steps as cGameSpyAdmin::Connect_To_Game_Server (private), which --connect reaches through
// cGameSpyAdmin::Think. cNetwork::Init_Client applies the password attempt because Is_Gamespy_Game() is true
// while the Internet menus are up (IsUnderGamespyMenuing).
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::Connect_To_Server (void)
{
	//
	//	Stop querying; we are leaving the list
	//
	RefreshPending = false;
	ServerBrowser.Cancel ();

	cGameSpyAdmin::Set_Game_Host_Ip (PendingJoin.Address);
	cGameSpyAdmin::Set_Game_Host_Port (PendingJoin.GamePort);

	if (PTheGameData != nullptr) {
		delete PTheGameData;
		PTheGameData = nullptr;
	}

	GameInitMgrClass::Initialize_LAN ();

	PTheGameData = cGameData::Create_Game_Of_Type (cGameData::GAME_TYPE_CNC);
	WWASSERT(PTheGameData != nullptr);
	PTheGameData->Set_Ip_Address (PendingJoin.Address);
	PTheGameData->Set_Port (PendingJoin.GamePort);

	//
	//	The connect starts while the Internet menus are up (GameSpy mode), but the player name is sent again
	// after they close, when the client reads the LAN nickname. Set both, and the password directly.
	//
	cNetInterface::Set_Lan_And_Internet_Nickname (JoinNickname);
	PTheGameData->Set_Password (cGameSpyAdmin::Get_Password_Attempt ());

	cNetwork::Init_Client ();

	//
	//	Display the "connecting" dialog
	//
	DlgMPConnect::DoDialog (-1, 0);
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_ListCtrl_DblClk
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_ListCtrl_DblClk
(
	ListCtrlClass * /* list_ctrl */,
	int				/* ctrl_id */,
	int				/* item_index */
)
{
	Join_Game ();
	return ;
}


////////////////////////////////////////////////////////////////
//
//	On_EditCtrl_Change
//
////////////////////////////////////////////////////////////////
void
InternetServerListMenuClass::On_EditCtrl_Change (EditCtrlClass *edit, int id)
{
	if (IDC_NICKNAME_EDIT == id) {

		//
		// Do not allow leading or trailing whitespace
		//
		WideStringClass text (0, true);
		text = edit->Get_Text ();
		text.Trim ();
		edit->Set_Text (text);

		bool enable = (edit->Get_Text_Length () > 0);
		Enable_Dlg_Item (IDC_JOIN_GAME_BUTTON, enable);
	}

	return ;
}

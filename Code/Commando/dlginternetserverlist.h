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
** Internet server list dialog.
**
** Shows the joinable servers found by ServerBrowser (see serverbrowser.h) and connects to the selected one.
** Opened from the Internet (GameSpy) main menu Join button.
*/

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef __DLG_INTERNET_SERVER_LIST_H
#define __DLG_INTERNET_SERVER_LIST_H


#include "menudialog.h"
#include "serverbrowser.h"
#include "widestring.h"
#include <wwlib/Signaler.h>
#include <string>
#include <vector>

class DlgPasswordPrompt;

////////////////////////////////////////////////////////////////
//
//	InternetServerListMenuClass
//
////////////////////////////////////////////////////////////////
class InternetServerListMenuClass : public MenuDialogClass,
	protected Signaler<DlgPasswordPrompt>
{
public:

	////////////////////////////////////////////////////////////////
	//	Public constructors/destructors
	////////////////////////////////////////////////////////////////
	InternetServerListMenuClass (void);
	~InternetServerListMenuClass (void);

	////////////////////////////////////////////////////////////////
	//	Public methods
	////////////////////////////////////////////////////////////////
	void		On_Init_Dialog (void) override;
	void		On_Destroy (void) override;
	void		On_Command (int ctrl_id, int mesage_id, unsigned int param) override;
	bool		On_Key_Down (uint32 key_id, uint32 key_data) override;

	void		On_ListCtrl_DblClk (ListCtrlClass *list_ctrl, int ctrl_id, int item_index) override;
	void		On_EditCtrl_Change (EditCtrlClass *edit, int id) override;

protected:

	////////////////////////////////////////////////////////////////
	//	Protected methods
	////////////////////////////////////////////////////////////////
	void		On_Frame_Update (void) override;
	void		ReceiveSignal (DlgPasswordPrompt &) override;

	void		Start_Refresh (void);
	void		Update_Status (void);
	void		Update_Server_List (void);
	void		Set_Status_Text (const WideStringClass &text);
	void		Join_Game (void);
	void		Connect_To_Server (void);

	static std::string	Build_Master_List (const char *heartbeat_list);

	////////////////////////////////////////////////////////////////
	//	Protected member data
	////////////////////////////////////////////////////////////////
	std::vector<ServerBrowserEntry>	Servers;			// what the list control shows, sorted by ping
	ServerBrowserClass::StateType		LastState;		// browser state seen on the previous frame
	int										LastListedCount;
	bool										RefreshPending;	// asked for a refresh that has not finished yet
	WideStringClass						StatusText;
	ServerBrowserEntry					PendingJoin;	// server waiting on the password prompt
	WideStringClass						JoinNickname;	// nickname from the edit box when Join was pressed
};


#endif //__DLG_INTERNET_SERVER_LIST_H

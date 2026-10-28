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

/******************************************************************************
*
* FILE
*     $Archive: /Commando/Code/Commando/DlgMPTeamSelect.cpp $
*
* DESCRIPTION
*     Multiplayer team selection dialog.
*
* PROGRAMMER
*     Denzil E. Long, Jr.
*     $Author: Denzil_l $
*
* VERSION INFO
*     $Revision: 17 $
*     $Modtime: 2/24/02 2:51p $
*
******************************************************************************/

#include "DlgMPTeamSelect.h"
#include "renegadedialog.h"
#include "gamemode.h"
#include "gamedata.h"
#include "player.h"
#include "timemgr.h"
#include "miscutil.h"
#include <Combat/playertype.h>
#include <wwui/listctrl.h>
#include <wwui/comboboxctrl.h>
#include <wwui/imagectrl.h>
#include "string_ids.h"
#include <wwtranslatedb/translatedb.h>


// Player list columns
enum
	{
	COL_RANK,
	COL_NAME,
// Denzil 02/24/02 Day 1 patch - Remove clan until we can fix formating
//	COL_CLAN,
	COL_KD,
	COL_SCORE
	};


static int ListSortCallback(ListCtrlClass* list, int index1, int index2, uint32 /* param */)
	{
	int rank1 = (int)list->Get_Entry_Data(index1, COL_RANK);
	int rank2 = (int)list->Get_Entry_Data(index2, COL_RANK);
	return (rank1 - rank2);
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::DoDialog
*
* DESCRIPTION
*     Display team selection / team profile dialog
*
* INPUTS
*     Target - Target to receive user choice signal.
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::DoDialog(Signaler<MPChooseTeamSignal>& target)
	{
	DlgMPTeamSelect* dialog = new DlgMPTeamSelect;

	if (dialog)
		{
		if (dialog->FinalizeCreate())
			{
			dialog->Start_Dialog();
			dialog->SignalMe(target);
			}

		dialog->Release_Ref();
		}
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::DlgMpTeamSelect
*
* DESCRIPTION
*     Constructor
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

DlgMPTeamSelect::DlgMPTeamSelect(void) :
		MenuDialogClass(GetRenegadeDialog(RenegadeDialogID::IDD_MP_TEAM_SELECT)),
		mCanChoose(true),
		mTimeRemaining(0.0f)
	{
	WWDEBUG_SAY(("DlgMPTeamSelect instantiated\n"));
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::~DlgMPTeamSelect
*
* DESCRIPTION
*     Destructor
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

DlgMPTeamSelect::~DlgMPTeamSelect()
	{
	WWDEBUG_SAY(("DlgMPTeamSelect destroyed\n"));
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::FinalizeCreate
*
* DESCRIPTION
*     Post creation initialization.
*
* INPUTS
*     NONE
*
* RESULT
*     True if successful
*
******************************************************************************/

bool DlgMPTeamSelect::FinalizeCreate(void)
	{
	const cGameData* theGame = The_Game();

	if (theGame == nullptr)
		{
		return false;
		}

	mCanChoose = theGame->IsTeamChangingAllowed.Get();

	return true;
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::On_Init_Dialog
*
* DESCRIPTION
*     One time dialog initialization
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::On_Init_Dialog(void)
	{
	WWDEBUG_SAY(("DlgMPTeamSelect On_Init_Dialog\n"));

	MenuDialogClass::On_Init_Dialog();

	// Setup the team icons on the dialog
	((ImageCtrlClass*)Get_Dlg_Item(IDC_GDI_TEAM_ICON))->Set_Texture("HUD_C&C_GDILOGO.TGA");
	((ImageCtrlClass*)Get_Dlg_Item(IDC_NOD_TEAM_ICON))->Set_Texture("HUD_C&C_NODLOGO.TGA");

	// Setup the list controls
	ListCtrlClass* list = (ListCtrlClass*)Get_Dlg_Item(IDC_GDI_LIST_CTRL);

	if (list)
		{
		// Configure the columns
		list->Add_Column(TRANSLATE (IDS_MENU_RANK),  0.15F, Vector3(1, 1, 1));

// Denzil 02/24/02 Day 1 patch - Remove clan until we can fix formatting issues.
#if(0)
		list->Add_Column(TRANSLATE (IDS_MENU_NAME),  0.30F, Vector3(1, 1, 1));
		list->Add_Column(TRANSLATE (IDS_MENU_CLAN),  0.20F, Vector3(1, 1, 1));
#else
		list->Add_Column(TRANSLATE (IDS_MENU_NAME),  0.50F, Vector3(1, 1, 1));
#endif

		list->Add_Column(TRANSLATE (IDS_MENU_KD_RATIO),   0.15F, Vector3(1, 1, 1));
		list->Add_Column(TRANSLATE (IDS_MENU_SCORE), 0.20F, Vector3(1, 1, 1));
		}

	list = (ListCtrlClass*)Get_Dlg_Item(IDC_NOD_LIST_CTRL);

	if (list)
		{
		// Configure the columns
		list->Add_Column(TRANSLATE (IDS_MENU_RANK),  0.15F, Vector3(1, 1, 1));

// Denzil 02/24/02 Day 1 patch - Remove clan until we can fix formatting issues.
#if(0)
		list->Add_Column(TRANSLATE (IDS_MENU_NAME),  0.30F, Vector3(1, 1, 1));
		list->Add_Column(TRANSLATE (IDS_MENU_CLAN),  0.20F, Vector3(1, 1, 1));
#else
		list->Add_Column(TRANSLATE (IDS_MENU_NAME),  0.50F, Vector3(1, 1, 1));
#endif

		list->Add_Column(TRANSLATE (IDS_MENU_KD_RATIO),   0.15F, Vector3(1, 1, 1));
		list->Add_Column(TRANSLATE (IDS_MENU_SCORE), 0.20F, Vector3(1, 1, 1));
		}

	int sidePref = cNetInterface::Get_Side_Preference();

	// Hide and disable the back button for LAN games.
	DialogControlClass* ctrl = Get_Dlg_Item(IDCANCEL);
	WWASSERT(ctrl != nullptr);
	ctrl->Show(false);
	ctrl->Enable(false);

	cPlayerManager::Add_Event_Observer(*this);

	PopulateWithLANPlayers();

	InitSideChoice(sidePref);

	//	Activate the menu game mode (if necessary)
	GameModeClass* menuMode = GameModeManager::Find("Menu");

	if (menuMode && !menuMode->Is_Active())
		{
		menuMode->Activate();
		}
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::On_Frame_Update
*
* DESCRIPTION
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::On_Frame_Update(void)
	{
	if (mTimeRemaining >= WWMATH_EPSILON)
		{
		mTimeRemaining -= TimeManager::Get_Frame_Real_Seconds();
		ShowTimeRemaining(mTimeRemaining);
		}

	MenuDialogClass::On_Frame_Update();
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::On_Command
*
* DESCRIPTION
*
* INPUTS
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::On_Command(int ctrlID, int message, unsigned int param)
	{
	switch (ctrlID)
		{
		case IDCANCEL:
		case IDC_STARTGAME:
			{
			int side = GetSideChoice();
			MPChooseTeamSignal chooseSignal((IDC_STARTGAME == ctrlID), side);
			Signaler<MPChooseTeamSignal>::SendSignal(chooseSignal);
			End_Dialog();
			}
			break;

		case IDC_TEAM_AUTO_CHECK:
			SelectSideChoice(PLAYERTYPE_RENEGADE);
			break;

		case IDC_TEAM_GDI_CHECK:
			SelectSideChoice(PLAYERTYPE_GDI);
			break;

		case IDC_TEAM_NOD_CHECK:
			SelectSideChoice(PLAYERTYPE_NOD);
			break;
		}

	MenuDialogClass::On_Command(ctrlID, message, param);
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::On_Last_Menu_Ending
*
* DESCRIPTION
*     Callback from menu class signifying the close of the last menu
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::On_Last_Menu_Ending(void)
	{
	// Nothing to do for LAN / internet games; control returns to the game.
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::InitSideChoice
*
* DESCRIPTION
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::InitSideChoice(int sidePref)
	{
	Enable_Dlg_Item(IDC_TEAM_AUTO_CHECK, mCanChoose);
	Enable_Dlg_Item(IDC_TEAM_GDI_CHECK, mCanChoose);
	Enable_Dlg_Item(IDC_TEAM_NOD_CHECK, mCanChoose);

	// Default side selection combo to the users preference.
	int side = ((mCanChoose == true) ? sidePref : -1);
	SelectSideChoice(side);
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::SelectSideChoice
*
* DESCRIPTION
*
* INPUTS
*     Side -
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::SelectSideChoice(int side)
	{
	Check_Dlg_Button(IDC_TEAM_AUTO_CHECK, (PLAYERTYPE_RENEGADE == side));
	Check_Dlg_Button(IDC_TEAM_GDI_CHECK, (PLAYERTYPE_GDI == side));
	Check_Dlg_Button(IDC_TEAM_NOD_CHECK, (PLAYERTYPE_NOD == side));
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::GetSideChoice
*
* DESCRIPTION
*
* INPUTS
*     Side -
*
* RESULT
*     NONE
*
******************************************************************************/

int DlgMPTeamSelect::GetSideChoice(void)
	{
	if (Is_Dlg_Button_Checked(IDC_TEAM_GDI_CHECK))
		{
		return PLAYERTYPE_GDI;
		}
	else if (Is_Dlg_Button_Checked(IDC_TEAM_NOD_CHECK))
		{
		return PLAYERTYPE_NOD;
		}

	return PLAYERTYPE_RENEGADE;
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::ShowTimeRemaining
*
* DESCRIPTION
*     Set the time remaining text
*
* INPUTS
*     Seconds - Seconds remaining in the game
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::ShowTimeRemaining(float remainingSeconds)
	{
	int hours = 0;
	int mins = 0;
	int seconds = 0;

	cMiscUtil::Seconds_To_Hms(remainingSeconds, hours, mins, seconds);

	WideStringClass timeString(0, true);
	timeString.Format(U_CHAR("%02d:%02d:%02d"), hours, mins, seconds);

	WideStringClass text(0, true);
	text.Format(U_CHAR("%s: %s"), TRANSLATION(IDS_MP_TIME_REMAINING), (const unichar_t*)timeString);
	Set_Dlg_Item_Text(IDC_TIME_REMAINING_TEXT, (const unichar_t*)text);
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::FindPlayerInListCtrl
*
* DESCRIPTION
*     Find a player entry in the player (GDI or Nod) list control.
*
* INPUTS
*     Name  - Name of player to look for.
*     List  - On return; List control containing player entry.
*     Index - On return; Player entry list index
*
* RESULT
*     True if player found in one of the lists.
*
******************************************************************************/

bool DlgMPTeamSelect::FindPlayerInListCtrl(const unichar_t* name, ListCtrlClass*& outList, int& outIndex)
	{
	// Check in GDI player list
	ListCtrlClass* list = (ListCtrlClass*)Get_Dlg_Item(IDC_GDI_LIST_CTRL);

	if (list)
		{
		int index = list->Find_Entry(COL_NAME, name);

		if (index != -1)
			{
			outList = list;
			outIndex = index;
			return true;
			}
		}

	// Check in Nod player list
	list = (ListCtrlClass*)Get_Dlg_Item(IDC_NOD_LIST_CTRL);

	if (list)
		{
		int index = list->Find_Entry(COL_NAME, name);

		if (index != -1)
			{
			outList = list;
			outIndex = index;
			return true;
			}
		}

	return false;
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::HandleNotification(PlayerMgrEvent)
*
* DESCRIPTION
*
* INPUTS
*
* RESULT
*
******************************************************************************/

void DlgMPTeamSelect::HandleNotification(PlayerMgrEvent& event)
	{
	PLAYERMGR_ACTION action = event.GetAction();

	if (action == PLAYER_ACTIVATED)// || (action == PLAYER_ADDED))
		{
		AddLANPlayerInfo(event.Subject());
		}
	else if ((action == PLAYER_DEACTIVATED) || (action == PLAYER_REMOVED))
		{
		RemoveLANPlayerInfo(event.Subject());
		}
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::ProcessWithLANPlayers
*
* DESCRIPTION
*
* INPUTS
*     NONE
*
* RESULT
*     NONE
*
******************************************************************************/

void DlgMPTeamSelect::PopulateWithLANPlayers(void)
	{
	SList<cPlayer>* playerList = cPlayerManager::Get_Player_Object_List();
	SLNode<cPlayer>* playerNode = playerList->Head();

	while (playerNode)
		{
		cPlayer* player = playerNode->Data();

		if (player->Get_Is_Active().Is_True())
			{
			AddLANPlayerInfo(player);
			}

		playerNode = playerNode->Next();
		}
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::AddLANPlayerInfo
*
* DESCRIPTION
*
* INPUTS
*
* RESULT
*
******************************************************************************/

void DlgMPTeamSelect::AddLANPlayerInfo(cPlayer* player)
	{
	WWASSERT(player != nullptr);
	RemoveLANPlayerInfo(player);

	int playerType = player->Get_Player_Type();
	int listID = ((playerType == PLAYERTYPE_GDI) ? IDC_GDI_LIST_CTRL : IDC_NOD_LIST_CTRL);

	ListCtrlClass* list = (ListCtrlClass*)Get_Dlg_Item(listID);
	WWASSERT(list != nullptr);

	int itemIndex = list->Insert_Entry(list->Get_Entry_Count(), U_CHAR(""));

	if (itemIndex >= 0)
		{
		list->Set_Entry_Text(itemIndex, COL_NAME, player->Get_Name());
		list->Set_Entry_Int(itemIndex, COL_RANK, player->Get_Rung());
		list->Set_Entry_Int(itemIndex, COL_SCORE, int(player->Get_Score()));

		WideStringClass text(0, true);
		text.Format(U_CHAR("%d/%d"), player->Get_Kills(), player->Get_Deaths());
		list->Set_Entry_Text(itemIndex, COL_KD, text);

		// If this is the player client the mark there name with a star
		const WideStringClass& nickname = cNetInterface::Get_Nickname();

		if (nickname.Compare_No_Case(player->Get_Name()) == 0)
			{
			list->Add_Icon(itemIndex, COL_NAME, "IF_LRGSTAR.TGA");
			list->Set_Entry_Color(itemIndex, COL_RANK, Vector3(1.0F, 1.0F, 1.0F));
			list->Set_Entry_Color(itemIndex, COL_NAME, Vector3(1.0F, 1.0F, 1.0F));
			list->Set_Entry_Color(itemIndex, COL_KD, Vector3(1.0F, 1.0F, 1.0F));
			list->Set_Entry_Color(itemIndex, COL_SCORE, Vector3(1.0F, 1.0F, 1.0F));

			SelectSideChoice(playerType);
			}
		}

	list->Sort(ListSortCallback, 0);
	}


/******************************************************************************
*
* NAME
*     DlgMPTeamSelect::RemoveLANPlayerInfo
*
* DESCRIPTION
*
* INPUTS
*
* RESULT
*
******************************************************************************/

void DlgMPTeamSelect::RemoveLANPlayerInfo(cPlayer* player)
	{
	WWASSERT(player != nullptr);

	ListCtrlClass* list = nullptr;
	int itemIndex = -1;
	bool found = FindPlayerInListCtrl(player->Get_Name(), list, itemIndex);

	if (found)
		{
		list->Delete_Entry(itemIndex);
		}
	}

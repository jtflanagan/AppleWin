/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2014, Tom Charlesworth, Michael Pohoreski, Nick Westgate
Copyright (C) 2023, Henri Asseily

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "StdAfx.h"

#include "PageSDHR.h"
#include "PropertySheet.h"

#include "../Common.h"
#include "../Registry.h"
#include "../resource/resource.h"

CPageSDHR* CPageSDHR::ms_this = 0;	// reinit'd in ctor

INT_PTR CALLBACK CPageSDHR::DlgProc(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	// Switch from static func to our instance
	return CPageSDHR::ms_this->DlgProcInternal(hWnd, message, wparam, lparam);
}


INT_PTR CPageSDHR::DlgProcInternal(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_NOTIFY:
	{
		// Property Sheet notifications

		switch (((LPPSHNOTIFY)lparam)->hdr.code)
		{
		case PSN_SETACTIVE:
			// About to become the active page
			m_PropertySheetHelper.SetLastPage(m_Page);
			break;
		case PSN_KILLACTIVE:
			SetWindowLongPtr(hWnd, DWLP_MSGRESULT, FALSE);			// Changes are valid
			break;
		case PSN_APPLY:
			DlgOK(hWnd);
			SetWindowLongPtr(hWnd, DWLP_MSGRESULT, PSNRET_NOERROR);	// Changes are valid
			break;
		case PSN_QUERYCANCEL:
			// Can use this to ask user to confirm cancel
			break;
		case PSN_RESET:
			DlgCANCEL(hWnd);
			break;
		}
	}
	break;

	case WM_COMMAND:
		switch (LOWORD(wparam))
		{
		case IDC_SDHR_REMOTE_ENABLED:
			break;
		case IDC_SDHR_REMOTE_IP:
			break;
		case IDC_SDHR_REMOTE_PORT:
			break;
		}
		break;

	case WM_INITDIALOG:
	{
		DWORD m_isEnabled;
		REGLOAD_DEFAULT(TEXT(REGVALUE_SAVE_STATE_ON_EXIT), &m_isEnabled, 0);
		CheckDlgButton(hWnd, IDC_SDHR_REMOTE_ENABLED, m_isEnabled == 1 ? BST_CHECKED : BST_UNCHECKED);
		// TODO: Text Fields

		break;
	}
	}

	return FALSE;
}

void CPageSDHR::DlgOK(HWND hWnd)
{
	const SoundType_e newSoundType = (SoundType_e)SendDlgItemMessage(hWnd, IDC_SOUNDTYPE, CB_GETCURSEL, 0, 0);

	const DWORD dwSpkrVolume = SendDlgItemMessage(hWnd, IDC_SPKR_VOLUME, TBM_GETPOS, 0, 0);
	const DWORD dwMBVolume = SendDlgItemMessage(hWnd, IDC_MB_VOLUME, TBM_GETPOS, 0, 0);

	SpkrSetEmulationType(newSoundType);
	DWORD dwSoundType = (soundtype == SOUND_NONE) ? REG_SOUNDTYPE_NONE : REG_SOUNDTYPE_WAVE;
	REGSAVE(TEXT(REGVALUE_SOUND_EMULATION), dwSoundType);

	// NB. Volume: 0=Loudest, VOLUME_MAX=Silence
	SpkrSetVolume(dwSpkrVolume, VOLUME_MAX);
	GetCardMgr().GetMockingboardCardMgr().SetVolume(dwMBVolume, VOLUME_MAX);

	REGSAVE(TEXT(REGVALUE_SPKR_VOLUME), SpkrGetVolume());
	REGSAVE(TEXT(REGVALUE_MB_VOLUME), GetCardMgr().GetMockingboardCardMgr().GetVolume());

	m_PropertySheetHelper.PostMsgAfterClose(hWnd, m_Page);
}


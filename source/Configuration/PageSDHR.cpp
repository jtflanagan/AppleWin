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
#include <string>

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
		{
			BOOL _isSuccess;
			auto _remotePort = GetDlgItemInt(hWnd, IDC_SDHR_REMOTE_PORT, &_isSuccess, false);
			if (!_isSuccess)
				SetDlgItemInt(hWnd, IDC_SDHR_REMOTE_PORT, 8080, false);
			break;
		}
		}
		break;

	case WM_INITDIALOG:
	{
		REGLOAD_DEFAULT(TEXT(REGVALUE_SDHR_REMOTE_ENABLED), &m_isEnabled, 0);
		CheckDlgButton(hWnd, IDC_SDHR_REMOTE_ENABLED, m_isEnabled == 1 ? BST_CHECKED : BST_UNCHECKED);

		CHAR _remoteIp[16] = "";
		RegLoadString(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_IP), 1, _remoteIp, 15);
		SetDlgItemText(hWnd, IDC_SDHR_REMOTE_IP, _remoteIp);
		m_SDHRNetworkIP = _remoteIp;

		m_SDHRNetworkPort = 8080;
		RegLoadValue(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_PORT), 1, &m_SDHRNetworkPort);
		SetDlgItemInt(hWnd, IDC_SDHR_REMOTE_PORT, m_SDHRNetworkPort, false);

		break;
	}
	}

	return FALSE;
}

void CPageSDHR::DlgOK(HWND hWnd)
{
	if (IsDlgButtonChecked(hWnd, IDC_SDHR_REMOTE_ENABLED) == BST_CHECKED)
		m_isEnabled = 1;
	else
		m_isEnabled = 0;
	REGSAVE(TEXT(REGVALUE_SDHR_REMOTE_ENABLED), m_isEnabled);

	CHAR _remoteIp[16] = "";
	GetDlgItemText(hWnd, IDC_SDHR_REMOTE_IP, _remoteIp, 15);
	RegSaveString(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_IP), 1, _remoteIp);
	m_SDHRNetworkIP = _remoteIp;

	BOOL _isSuccess;
	auto _portTemp = GetDlgItemInt(hWnd, IDC_SDHR_REMOTE_PORT, &_isSuccess, false);
	if (_isSuccess)
	{
		m_SDHRNetworkPort = _portTemp;
		RegSaveValue(TEXT(REG_PREFS), TEXT(REGVALUE_SDHR_REMOTE_PORT), 1, m_SDHRNetworkPort);
	}
	m_PropertySheetHelper.PostMsgAfterClose(hWnd, m_Page);
}


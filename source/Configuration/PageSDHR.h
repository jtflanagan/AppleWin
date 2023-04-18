#pragma once

#include "IPropertySheetPage.h"
#include "PropertySheetDefs.h"

#include <string>

class CPropertySheetHelper;

class CPageSDHR : private IPropertySheetPage
{
public:
	CPageSDHR(CPropertySheetHelper& PropertySheetHelper) :
		m_Page(PG_SDHR),
		m_PropertySheetHelper(PropertySheetHelper)
	{
		CPageSDHR::ms_this = this;
	}
	virtual ~CPageSDHR() {}

	static INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam);

	BOOL GetIsSDHRNetworked(void) { return (m_isEnabled == 1) ; }
	std::string GetSDHRNetworkIp(void) { return m_SDHRNetworkIP; }
	UINT GetSDHRNetworkPort(void) { return m_SDHRNetworkPort; }

protected:
	// IPropertySheetPage
	virtual INT_PTR DlgProcInternal(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam);
	virtual void DlgOK(HWND hWnd);
	virtual void DlgCANCEL(HWND hWnd) {}

private:
	static CPageSDHR* ms_this;

	const PAGETYPE m_Page;
	CPropertySheetHelper& m_PropertySheetHelper;
	DWORD m_isEnabled;
	std::string m_SDHRNetworkIP;
	DWORD m_SDHRNetworkPort;
};

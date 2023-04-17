#pragma once

#include "IPropertySheetPage.h"
#include "PropertySheetDefs.h"

#include <string>

class CPropertySheetHelper;

class CPageSDHR : private IPropertySheetPage
{
public:
	CPageSDHR(CPropertySheetHelper& PropertySheetHelper) :
		m_Page(PG_SOUND),
		m_PropertySheetHelper(PropertySheetHelper)
	{
		CPageSDHR::ms_this = this;
	}
	virtual ~CPageSDHR() {}

	static INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam);

protected:
	// IPropertySheetPage
	virtual INT_PTR DlgProcInternal(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam);
	virtual void DlgOK(HWND hWnd);
	virtual void DlgCANCEL(HWND hWnd) {}

private:
	static CPageSDHR* ms_this;

	const PAGETYPE m_Page;
	CPropertySheetHelper& m_PropertySheetHelper;
};

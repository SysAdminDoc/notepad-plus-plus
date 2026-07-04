// This file is part of Notepad++ project
// Copyright (C)2021 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "StatusBar.h"

#include <windows.h>

#include <uxtheme.h>
#include <vsstyle.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "DoubleBuffer/DoubleBuffer.h"
#include "NppConstants.h"
#include "NppDarkMode.h"
#include "Window.h"
#include "dpiManagerV2.h"

//#define IDC_STATUSBAR 789


enum
{
	defaultPartWidth = 5,
};

static constexpr COLORREF mochaBase = 0x002e1e1e;     // #1e1e2e
static constexpr COLORREF mochaMantle = 0x00251818;   // #181825
static constexpr COLORREF mochaSurface0 = 0x00443231; // #313244
static constexpr COLORREF mochaText = 0x00f4d6cd;     // #cdd6f4
static constexpr COLORREF mochaSubtext0 = 0x00c8ada6; // #a6adc8
static constexpr COLORREF mochaBlue = 0x00fab489;     // #89b4fa

static RECT insetRect(RECT rc, int dx, int dy)
{
	::InflateRect(&rc, -dx, -dy);
	if (rc.right < rc.left)
		rc.right = rc.left;
	if (rc.bottom < rc.top)
		rc.bottom = rc.top;
	return rc;
}

static void paintRoundedRect(HDC hdc, const RECT& rc, HPEN hPen, HBRUSH hBrush, int radius)
{
	auto holdPen = static_cast<HPEN>(::SelectObject(hdc, hPen));
	auto holdBrush = static_cast<HBRUSH>(::SelectObject(hdc, hBrush));
	::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
	::SelectObject(hdc, holdBrush);
	::SelectObject(hdc, holdPen);
}

static void paintStatusSizeGrip(HDC hdc, const RECT& rcClient, int scale)
{
	HPEN hGripPen = ::CreatePen(PS_SOLID, 1, mochaSubtext0);
	auto holdPen = static_cast<HPEN>(::SelectObject(hdc, hGripPen));
	for (int i = 0; i < 3; ++i)
	{
		const int offset = scale * (3 + (i * 4));
		::MoveToEx(hdc, rcClient.right - offset, rcClient.bottom - scale, nullptr);
		::LineTo(hdc, rcClient.right - scale, rcClient.bottom - offset);
	}
	::SelectObject(hdc, holdPen);
	::DeleteObject(hGripPen);
}


StatusBar::~StatusBar()
{
	delete[] _lpParts;
}


void StatusBar::init(HINSTANCE, HWND)
{
	assert(false and "should never be called");
}


struct StatusBarSubclassInfo
{
	HTHEME hTheme = nullptr;
	HFONT _hFont = nullptr;

	StatusBarSubclassInfo() = default;
	explicit StatusBarSubclassInfo(const HFONT& hFont) noexcept
		: _hFont(hFont) {}

	~StatusBarSubclassInfo()
	{
		closeTheme();
		destroyFont();
	}

	bool ensureTheme(HWND hwnd)
	{
		if (!hTheme)
		{
			hTheme = ::OpenThemeData(hwnd, VSCLASS_STATUS);
		}
		return hTheme != nullptr;
	}

	void closeTheme()
	{
		if (hTheme)
		{
			CloseThemeData(hTheme);
			hTheme = nullptr;
		}
	}

	void setFont(const HFONT& hFont)
	{
		destroyFont();
		_hFont = hFont;
	}

	void destroyFont()
	{
		if (_hFont != nullptr)
		{
			::DeleteObject(_hFont);
			_hFont = nullptr;
		}
	}
};


static LRESULT CALLBACK StatusBarSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	StatusBarSubclassInfo* pStatusBarInfo = reinterpret_cast<StatusBarSubclassInfo*>(dwRefData);

	switch (uMsg)
	{
		case WM_ERASEBKGND:
		{
			if (!NppDarkMode::isEnabled())
			{
				break;  // Let the control paint background the default way
			}

			RECT rc{};
			::GetClientRect(hWnd, &rc);
			::FillRect(reinterpret_cast<HDC>(wParam), &rc, NppDarkMode::getBackgroundBrush());
			return TRUE;
		}

		case WM_PAINT:
		case WM_PRINTCLIENT:
		{
			if (!NppDarkMode::isEnabled())
			{
				break;  // Let the control paint itself the default way
			}

			PAINTSTRUCT ps{};
			HDC hdc = (uMsg == WM_PAINT) ? ::BeginPaint(hWnd, &ps) : reinterpret_cast<HDC>(wParam);

			const auto style = ::GetWindowLongPtr(hWnd, GWL_STYLE);
			bool isSizeGrip = style & SBARS_SIZEGRIP;

			auto holdFont = static_cast<HFONT>(::SelectObject(hdc, pStatusBarInfo->_hFont));

			RECT rcClient{};
			::GetClientRect(hWnd, &rcClient);
			HBRUSH hBaseBrush = ::CreateSolidBrush(mochaBase);
			HBRUSH hSegmentBrush = ::CreateSolidBrush(mochaMantle);
			HBRUSH hActiveBrush = ::CreateSolidBrush(mochaSurface0);
			HBRUSH hAccentBrush = ::CreateSolidBrush(mochaBlue);
			HPEN hSegmentPen = ::CreatePen(PS_SOLID, 1, mochaSurface0);
			HPEN hActivePen = ::CreatePen(PS_SOLID, 1, mochaBlue);

			::FillRect(hdc, &rcClient, hBaseBrush);

			const int marginX = DPIManagerV2::scale(4, hWnd);
			const int marginY = DPIManagerV2::scale(3, hWnd);
			const int textPadX = DPIManagerV2::scale(8, hWnd);
			const int radius = DPIManagerV2::scale(6, hWnd);
			const int accentHeight = std::max(1, DPIManagerV2::scale(2, hWnd));

			int nParts = static_cast<int>(SendMessage(hWnd, SB_GETPARTS, 0, 0));
			std::wstring str;
			for (int i = 0; i < nParts; ++i)
			{
				RECT rcPart{};
				::SendMessage(hWnd, SB_GETRECT, i, reinterpret_cast<LPARAM>(&rcPart));
				if (!::RectVisible(hdc, &rcPart))
				{
					continue;
				}

				DWORD cchText = 0;
				cchText = LOWORD(SendMessage(hWnd, SB_GETTEXTLENGTH, i, 0));
				str.resize(size_t{ cchText } + 1); // technically the std::wstring might not have an internal null character at the end of the buffer, so add one
				LRESULT lr = ::SendMessage(hWnd, SB_GETTEXT, i, reinterpret_cast<LPARAM>(str.data()));
				str.resize(cchText); // remove the extra NULL character
				bool ownerDraw = false;
				if (cchText == 0 && (lr & ~(SBT_NOBORDERS | SBT_POPOUT | SBT_RTLREADING)) != 0)
				{
					// this is a pointer to the text
					ownerDraw = true;
				}
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, (i == 2 || i == 5) ? mochaText : mochaSubtext0);

				RECT rcSegment = insetRect(rcPart, marginX, marginY);
				const bool isActivePart = (i == 2 || i == 5);
				paintRoundedRect(hdc, rcSegment, isActivePart ? hActivePen : hSegmentPen, isActivePart ? hActiveBrush : hSegmentBrush, radius);

				if (isActivePart)
				{
					RECT rcAccent = rcSegment;
					rcAccent.left += radius;
					rcAccent.right -= radius;
					rcAccent.top = std::max(rcAccent.top, rcAccent.bottom - accentHeight);
					if (rcAccent.right > rcAccent.left)
						::FillRect(hdc, &rcAccent, hAccentBrush);
				}

				RECT rcText = rcSegment;
				const int segmentWidth = rcSegment.right - rcSegment.left;
				const int actualTextPadX = std::min(textPadX, std::max(1, segmentWidth / 6));
				rcText.left += actualTextPadX;
				rcText.right -= actualTextPadX;

				if (ownerDraw)
				{
					UINT id = GetDlgCtrlID(hWnd);
					DRAWITEMSTRUCT dis = {
						0
						, 0
						, static_cast<UINT>(i)
						, ODA_DRAWENTIRE
						, id
						, hWnd
						, hdc
						, rcText
						, static_cast<ULONG_PTR>(lr)
					};

					SendMessage(GetParent(hWnd), WM_DRAWITEM, id, (LPARAM)&dis);
				}
				else
				{
					DrawText(hdc, str.c_str(), static_cast<int>(str.size()), &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
				}
			}

			if (isSizeGrip)
			{
				paintStatusSizeGrip(hdc, rcClient, std::max(1, DPIManagerV2::scale(2, hWnd)));
			}

			::SelectObject(hdc, holdFont);
			::DeleteObject(hActivePen);
			::DeleteObject(hSegmentPen);
			::DeleteObject(hAccentBrush);
			::DeleteObject(hActiveBrush);
			::DeleteObject(hSegmentBrush);
			::DeleteObject(hBaseBrush);

			if (uMsg == WM_PAINT)
			{
				::EndPaint(hWnd, &ps);
			}
			return 0;
		}

		case WM_NCDESTROY:
		{
			::RemoveWindowSubclass(hWnd, StatusBarSubclass, uIdSubclass);
			break;
		}

		case WM_DPICHANGED:
		case WM_DPICHANGED_AFTERPARENT:
		case WM_THEMECHANGED:
		{
			pStatusBarInfo->closeTheme();
			LOGFONT lf{ DPIManagerV2::getDefaultGUIFontForDpi(::GetParent(hWnd), DPIManagerV2::FontType::status) };
			pStatusBarInfo->setFont(::CreateFontIndirect(&lf));
			
			if (uMsg != WM_THEMECHANGED)
			{
				return 0;
			}
			break;
		}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}


void StatusBar::init(HINSTANCE hInst, HWND hPere, int nbParts)
{
	Window::init(hInst, hPere);
	InitCommonControls();

	// _hSelf = CreateStatusWindow(WS_CHILD | WS_CLIPSIBLINGS, NULL, _hParent, IDC_STATUSBAR);
	_hSelf = ::CreateWindowEx(
		0,
		STATUSCLASSNAME,
		L"",
		WS_CHILD | SBARS_SIZEGRIP ,
		0, 0, 0, 0,
		_hParent, nullptr, _hInst, 0);

	if (!_hSelf)
		throw std::runtime_error("StatusBar::init : CreateWindowEx() function return null");

	LOGFONT lf{ DPIManagerV2::getDefaultGUIFontForDpi(_hParent, DPIManagerV2::FontType::status) };
	StatusBarSubclassInfo* pStatusBarInfo = new StatusBarSubclassInfo(::CreateFontIndirect(&lf));
	_pStatusBarInfo = pStatusBarInfo;

	SetWindowSubclass(_hSelf, StatusBarSubclass, static_cast<UINT_PTR>(SubclassID::first), reinterpret_cast<DWORD_PTR>(pStatusBarInfo));

	DoubleBuffer::subclass(_hSelf);

	_partWidthArray.clear();
	if (nbParts > 0)
		_partWidthArray.resize(nbParts, defaultPartWidth);

	// Allocate an array for holding the right edge coordinates.
	if (_partWidthArray.size())
		_lpParts = new int[_partWidthArray.size()];

	RECT rc{};
	::GetClientRect(_hParent, &rc);
	adjustParts(rc.right);
}


bool StatusBar::setPartWidth(int whichPart, int width)
{
	if ((size_t) whichPart < _partWidthArray.size())
	{
		_partWidthArray[whichPart] = width;
		return true;
	}
	assert(false and "invalid status bar index");
	return false;
}


void StatusBar::destroy()
{
	::DestroyWindow(_hSelf);
	delete _pStatusBarInfo;
}


void StatusBar::reSizeTo(RECT& rc)
{
	::MoveWindow(_hSelf, rc.left, rc.top, rc.right, rc.bottom, TRUE);
	adjustParts(rc.right);
	redraw();
}


int StatusBar::getHeight() const
{
	return (FALSE != ::IsWindowVisible(_hSelf)) ? Window::getHeight() : 0;
}


void StatusBar::adjustParts(int clientWidth)
{
	// Calculate the right edge coordinate for each part, and
	// copy the coordinates to the array.
	int nWidth = std::max<int>(clientWidth - 20, 0);

	for (int i = static_cast<int>(_partWidthArray.size()) - 1; i >= 0; i--)
	{
		_lpParts[i] = nWidth;
		nWidth -= _partWidthArray[i];
	}

	// Tell the status bar to create the window parts.
	::SendMessage(_hSelf, SB_SETPARTS, _partWidthArray.size(), reinterpret_cast<LPARAM>(_lpParts));
}


bool StatusBar::setText(const wchar_t* str, int whichPart)
{
	if ((size_t) whichPart < _partWidthArray.size())
	{
		if (str != nullptr)
			_lastSetText = str;
		else
			_lastSetText.clear();

		return (TRUE == ::SendMessage(_hSelf, SB_SETTEXT, whichPart, reinterpret_cast<LPARAM>(_lastSetText.c_str())));
	}
	assert(false and "invalid status bar index");
	return false;
}


bool StatusBar::setOwnerDrawText(const wchar_t* str)
{
	if (str != nullptr)
		_lastSetText = str;
	else
		_lastSetText.clear();

	return (::SendMessage(_hSelf, SB_SETTEXT, SBT_OWNERDRAW, reinterpret_cast<LPARAM>(_lastSetText.c_str())) == TRUE);
}

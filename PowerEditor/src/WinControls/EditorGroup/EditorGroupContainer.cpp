// This file is part of Notepad++ project
// Copyright (C)2024 Don HO <don.h@free.fr>

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

#include "EditorGroupContainer.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

#include "AutoCompletion.h"
#include "DocTabView.h"
#include "NppDarkMode.h"
#include "ScintillaEditView.h"

static constexpr auto EGC_CLASS_NAME = L"nppEditorGroupContainer";
static constexpr int DROP_OVERLAY_ALPHA = 80;
static constexpr COLORREF DROP_OVERLAY_COLOR = RGB(60, 120, 200);
static constexpr double DROP_EDGE_FRACTION = 0.30;
static constexpr double MIN_GROUP_RATIO = 0.05;

bool EditorGroupContainer::_isRegistered = false;

void EditorGroupContainer::create(HINSTANCE hInst, HWND parent)
{
	Window::init(hInst, parent);

	if (!_isRegistered)
	{
		WNDCLASS wc{};
		wc.style = CS_DBLCLKS;
		wc.lpfnWndProc = staticWinProc;
		wc.hInstance = _hInst;
		wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszClassName = EGC_CLASS_NAME;

		if (!::RegisterClass(&wc))
			throw std::runtime_error("EditorGroupContainer::create: RegisterClass failed");

		_isRegistered = true;
	}

	_hSelf = ::CreateWindowEx(
		0, EGC_CLASS_NAME, L"EditorGroupContainer",
		WS_CHILD | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		_hParent, NULL, _hInst, this);

	if (!_hSelf)
		throw std::runtime_error("EditorGroupContainer::create: CreateWindowEx failed");
}


void EditorGroupContainer::destroy()
{
	hideDropOverlay();
	if (_hDropOverlay)
	{
		::DestroyWindow(_hDropOverlay);
		_hDropOverlay = nullptr;
	}
	::DestroyWindow(_hSelf);
}


void EditorGroupContainer::reSizeTo(RECT& rc)
{
	_lastRect = rc;
	::MoveWindow(_hSelf, rc.left, rc.top, rc.right, rc.bottom, FALSE);
	recalcLayout();
	applyLayout();
}


void EditorGroupContainer::display(bool toShow) const
{
	Window::display(toShow);
	for (const auto& g : _groups)
	{
		if (g.isValid())
		{
			g.docTab->display(toShow);
			g.editView->display(toShow);
		}
	}
}


void EditorGroupContainer::redraw(bool forceUpdate) const
{
	for (const auto& g : _groups)
	{
		if (g.isValid())
			g.docTab->redraw(forceUpdate);
	}
}


int EditorGroupContainer::addGroup(const EditorGroup& group)
{
	_groups.push_back(group);
	int idx = static_cast<int>(_groups.size()) - 1;

	normalizeRatios();

	if (::IsWindow(_hSelf) && _lastRect.right > 0)
	{
		recalcLayout();
		applyLayout();
	}
	return idx;
}


void EditorGroupContainer::removeGroup(int index)
{
	if (index < 0 || index >= static_cast<int>(_groups.size()))
		return;

	auto& g = _groups[index];

	g.docTab->display(false);
	g.editView->display(false);

	if (g.isDynamic)
	{
		if (g.autoComplete)
		{
			delete g.autoComplete;
			g.autoComplete = nullptr;
		}
		if (g.docTab)
		{
			g.docTab->destroy();
			delete g.docTab;
		}
		if (g.editView)
		{
			g.editView->destroy();
			delete g.editView;
		}
	}

	_groups.erase(_groups.begin() + index);

	if (_activeGroupIndex >= static_cast<int>(_groups.size()))
		_activeGroupIndex = static_cast<int>(_groups.size()) - 1;
	if (_activeGroupIndex < 0)
		_activeGroupIndex = 0;

	normalizeRatios();

	if (::IsWindow(_hSelf) && _lastRect.right > 0)
	{
		recalcLayout();
		applyLayout();
	}
}


EditorGroup* EditorGroupContainer::getGroupById(int id)
{
	for (auto& g : _groups)
	{
		if (g.id == id)
			return &g;
	}
	return nullptr;
}


int EditorGroupContainer::getGroupIndexById(int id) const
{
	for (int i = 0; i < static_cast<int>(_groups.size()); ++i)
	{
		if (_groups[i].id == id)
			return i;
	}
	return -1;
}


int EditorGroupContainer::getGroupIndexByEditView(const ScintillaEditView* view) const
{
	for (int i = 0; i < static_cast<int>(_groups.size()); ++i)
	{
		if (_groups[i].editView == view)
			return i;
	}
	return -1;
}


int EditorGroupContainer::getGroupIndexByDocTab(const DocTabView* tab) const
{
	for (int i = 0; i < static_cast<int>(_groups.size()); ++i)
	{
		if (_groups[i].docTab == tab)
			return i;
	}
	return -1;
}


int EditorGroupContainer::getGroupIndexByHwnd(HWND hwnd) const
{
	for (int i = 0; i < static_cast<int>(_groups.size()); ++i)
	{
		if (_groups[i].docTab && _groups[i].docTab->getHSelf() == hwnd)
			return i;
		if (_groups[i].editView && _groups[i].editView->getHSelf() == hwnd)
			return i;
	}
	return -1;
}


RECT EditorGroupContainer::getGroupRect(int index) const
{
	if (index >= 0 && index < static_cast<int>(_groupRects.size()))
		return _groupRects[index];
	return {};
}


DropZoneInfo EditorGroupContainer::hitTestDropZone(POINT screenPt) const
{
	DropZoneInfo info;
	POINT clientPt = screenPt;
	::ScreenToClient(_hSelf, &clientPt);

	for (int i = 0; i < static_cast<int>(_groupRects.size()); ++i)
	{
		const RECT& r = _groupRects[i];
		if (clientPt.x >= r.left && clientPt.x < r.left + r.right &&
			clientPt.y >= r.top && clientPt.y < r.top + r.bottom)
		{
			info.groupIndex = i;
			int edgeWidth = static_cast<int>(r.right * DROP_EDGE_FRACTION);
			int relX = clientPt.x - r.left;

			if (relX < edgeWidth)
			{
				info.position = DropPosition::Left;
				RECT overlay = { r.left, r.top, r.right / 2, r.bottom };
				info.overlayRect = overlay;
			}
			else if (relX > r.right - edgeWidth)
			{
				info.position = DropPosition::Right;
				RECT overlay = { r.left + r.right / 2, r.top, r.right / 2, r.bottom };
				info.overlayRect = overlay;
			}
			else
			{
				info.position = DropPosition::Center;
				info.overlayRect = r;
			}
			return info;
		}
	}
	return info;
}


void EditorGroupContainer::createDropOverlay()
{
	if (_hDropOverlay)
		return;

	_hDropOverlay = ::CreateWindowEx(
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		L"STATIC", nullptr,
		WS_POPUP,
		0, 0, 1, 1,
		_hSelf, NULL, _hInst, nullptr);

	::SetLayeredWindowAttributes(_hDropOverlay, 0, DROP_OVERLAY_ALPHA, LWA_ALPHA);
}


void EditorGroupContainer::showDropOverlay(const DropZoneInfo& info)
{
	if (info.position == DropPosition::None)
	{
		hideDropOverlay();
		return;
	}

	createDropOverlay();

	POINT origin = { info.overlayRect.left, info.overlayRect.top };
	::ClientToScreen(_hSelf, &origin);

	// Set background color
	HBRUSH hBrush = ::CreateSolidBrush(DROP_OVERLAY_COLOR);
	HDC hdc = ::GetDC(_hDropOverlay);
	RECT fillRect = { 0, 0, info.overlayRect.right, info.overlayRect.bottom };
	::FillRect(hdc, &fillRect, hBrush);
	::ReleaseDC(_hDropOverlay, hdc);
	::DeleteObject(hBrush);

	::SetWindowPos(_hDropOverlay, HWND_TOPMOST,
		origin.x, origin.y,
		info.overlayRect.right, info.overlayRect.bottom,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);

	_dropOverlayVisible = true;
}


void EditorGroupContainer::hideDropOverlay()
{
	if (_hDropOverlay && _dropOverlayVisible)
	{
		::ShowWindow(_hDropOverlay, SW_HIDE);
		_dropOverlayVisible = false;
	}
}


void EditorGroupContainer::normalizeRatios()
{
	if (_groups.empty())
		return;

	double total = 0.0;
	for (auto& g : _groups)
	{
		if (g.widthRatio < MIN_GROUP_RATIO)
			g.widthRatio = MIN_GROUP_RATIO;
		total += g.widthRatio;
	}

	if (total <= 0.0)
	{
		double equal = 1.0 / _groups.size();
		for (auto& g : _groups)
			g.widthRatio = equal;
	}
	else
	{
		for (auto& g : _groups)
			g.widthRatio /= total;
	}
}


void EditorGroupContainer::recalcLayout()
{
	RECT clientRect{};
	getClientRect(clientRect);
	int viewWidth = clientRect.right;
	int totalHeight = clientRect.bottom;

	int n = static_cast<int>(_groups.size());
	_groupRects.resize(n);
	_splitterRects.resize(std::max(0, n - 1));

	if (n == 0 || viewWidth <= 0 || totalHeight <= 0)
		return;

	int totalSplitterWidth = (n - 1) * _splitterGap;

	int neededWidth = n * _minColumnWidth + totalSplitterWidth;
	bool needsScroll = neededWidth > viewWidth;

	int layoutWidth = needsScroll ? neededWidth : viewWidth;
	int availableWidth = layoutWidth - totalSplitterWidth;
	if (availableWidth < n)
		availableWidth = n;

	normalizeRatios();

	int xPos = 0;
	for (int i = 0; i < n; ++i)
	{
		int groupWidth;
		if (needsScroll)
		{
			groupWidth = _minColumnWidth;
		}
		else if (i == n - 1)
		{
			groupWidth = layoutWidth - xPos;
		}
		else
		{
			groupWidth = static_cast<int>(std::round(_groups[i].widthRatio * availableWidth));
			if (groupWidth < 1) groupWidth = 1;
		}

		_groupRects[i] = { xPos, 0, groupWidth, totalHeight };
		xPos += groupWidth;

		if (i < n - 1)
		{
			_splitterRects[i] = { xPos, 0, _splitterGap, totalHeight };
			xPos += _splitterGap;
		}
	}
	_totalContentWidth = xPos;
}


void EditorGroupContainer::applyLayout() const
{
	RECT containerRect{};
	::GetWindowRect(_hSelf, &containerRect);
	POINT containerOrigin = { containerRect.left, containerRect.top };
	::ScreenToClient(_hParent, &containerOrigin);

	for (int i = 0; i < static_cast<int>(_groups.size()); ++i)
	{
		if (i < static_cast<int>(_groupRects.size()) && _groups[i].isValid())
		{
			RECT rc = _groupRects[i];
			rc.left += containerOrigin.x - _scrollOffset;
			rc.top += containerOrigin.y;
			_groups[i].docTab->reSizeTo(rc);
			::SetWindowPos(_groups[i].docTab->getHSelf(), HWND_TOP, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}
	::InvalidateRect(_hSelf, nullptr, TRUE);
}


void EditorGroupContainer::updateScrollBar()
{
	RECT clientRect{};
	getClientRect(clientRect);
	int viewWidth = clientRect.right;

	if (_totalContentWidth > viewWidth && _groups.size() > 1)
	{
		SCROLLINFO si = {};
		si.cbSize = sizeof(si);
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = _totalContentWidth - 1;
		si.nPage = viewWidth;
		si.nPos = _scrollOffset;
		::SetScrollInfo(_hSelf, SB_HORZ, &si, TRUE);
		::ShowScrollBar(_hSelf, SB_HORZ, TRUE);
	}
	else
	{
		_scrollOffset = 0;
		::ShowScrollBar(_hSelf, SB_HORZ, FALSE);
	}
}


int EditorGroupContainer::splitterHitTest(POINT clientPt) const
{
	for (int i = 0; i < static_cast<int>(_splitterRects.size()); ++i)
	{
		const RECT& r = _splitterRects[i];
		int hitMargin = 3;
		if (clientPt.x >= r.left - hitMargin && clientPt.x <= r.left + r.right + hitMargin &&
			clientPt.y >= r.top && clientPt.y <= r.top + r.bottom)
		{
			return i;
		}
	}
	return -1;
}


LRESULT CALLBACK EditorGroupContainer::staticWinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_NCCREATE:
		{
			auto* pContainer = static_cast<EditorGroupContainer*>(
				reinterpret_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
			pContainer->_hSelf = hwnd;
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pContainer));
			return TRUE;
		}
		default:
		{
			auto* pContainer = reinterpret_cast<EditorGroupContainer*>(
				::GetWindowLongPtr(hwnd, GWLP_USERDATA));
			if (!pContainer)
				return ::DefWindowProc(hwnd, message, wParam, lParam);
			return pContainer->runProc(message, wParam, lParam);
		}
	}
}


LRESULT EditorGroupContainer::runProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_ERASEBKGND:
		{
			HDC hdc = reinterpret_cast<HDC>(wParam);
			COLORREF splitterColor = NppDarkMode::isEnabled() ? NppDarkMode::getBackgroundColor() : ::GetSysColor(COLOR_3DFACE);
			for (const auto& sr : _splitterRects)
			{
				RECT drawRect = { sr.left - _scrollOffset, sr.top, sr.left - _scrollOffset + sr.right, sr.top + sr.bottom };
				HBRUSH brush = ::CreateSolidBrush(splitterColor);
				::FillRect(hdc, &drawRect, brush);
				::DeleteObject(brush);
			}
			return 1;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = ::BeginPaint(_hSelf, &ps);
			COLORREF splitterColor = NppDarkMode::isEnabled() ? NppDarkMode::getBackgroundColor() : ::GetSysColor(COLOR_3DFACE);
			for (const auto& sr : _splitterRects)
			{
				RECT drawRect = { sr.left - _scrollOffset, sr.top, sr.left - _scrollOffset + sr.right, sr.top + sr.bottom };
				HBRUSH brush = ::CreateSolidBrush(splitterColor);
				::FillRect(hdc, &drawRect, brush);
				::DeleteObject(brush);
			}
			::EndPaint(_hSelf, &ps);
			return 0;
		}

		case WM_SETCURSOR:
		{
			POINT pt;
			::GetCursorPos(&pt);
			::ScreenToClient(_hSelf, &pt);
			if (splitterHitTest(pt) >= 0)
			{
				::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
				return TRUE;
			}
			break;
		}

		case WM_LBUTTONDOWN:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			int idx = splitterHitTest(pt);
			if (idx >= 0)
			{
				_isDraggingSplitter = true;
				_dragSplitterIndex = idx;
				_dragStartPoint = pt;
				_dragStartRatioLeft = _groups[idx].widthRatio;
				_dragStartRatioRight = _groups[idx + 1].widthRatio;
				::SetCapture(_hSelf);
				return 0;
			}
			break;
		}

		case WM_MOUSEMOVE:
		{
			if (_isDraggingSplitter && _dragSplitterIndex >= 0)
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				int deltaX = pt.x - _dragStartPoint.x;

				RECT clientRect{};
				getClientRect(clientRect);
				int n = static_cast<int>(_groups.size());
				int availableWidth = clientRect.right - (n - 1) * _splitterGap;
				if (availableWidth <= 0) break;

				double deltaRatio = static_cast<double>(deltaX) / availableWidth;
				double newLeft = _dragStartRatioLeft + deltaRatio;
				double newRight = _dragStartRatioRight - deltaRatio;

				if (newLeft < MIN_GROUP_RATIO) newLeft = MIN_GROUP_RATIO;
				if (newRight < MIN_GROUP_RATIO) newRight = MIN_GROUP_RATIO;

				_groups[_dragSplitterIndex].widthRatio = newLeft;
				_groups[_dragSplitterIndex + 1].widthRatio = newRight;

				normalizeRatios();
				recalcLayout();
				applyLayout();
				return 0;
			}
			break;
		}

		case WM_LBUTTONUP:
		{
			if (_isDraggingSplitter)
			{
				_isDraggingSplitter = false;
				_dragSplitterIndex = -1;
				::ReleaseCapture();
				return 0;
			}
			break;
		}

		case WM_SIZE:
		{
			recalcLayout();
			applyLayout();
			updateScrollBar();
			return 0;
		}

		case WM_HSCROLL:
		{
			RECT clientRect{};
			getClientRect(clientRect);
			int viewWidth = clientRect.right;
			int maxScroll = (_totalContentWidth > viewWidth) ? (_totalContentWidth - viewWidth) : 0;

			int newPos = _scrollOffset;
			switch (LOWORD(wParam))
			{
				case SB_LINELEFT:    newPos -= 40; break;
				case SB_LINERIGHT:   newPos += 40; break;
				case SB_PAGELEFT:    newPos -= viewWidth / 2; break;
				case SB_PAGERIGHT:   newPos += viewWidth / 2; break;
				case SB_THUMBTRACK:
				case SB_THUMBPOSITION: newPos = HIWORD(wParam); break;
				case SB_LEFT:        newPos = 0; break;
				case SB_RIGHT:       newPos = maxScroll; break;
			}
			if (newPos < 0) newPos = 0;
			if (newPos > maxScroll) newPos = maxScroll;
			if (newPos != _scrollOffset)
			{
				_scrollOffset = newPos;
				applyLayout();
				updateScrollBar();
			}
			return 0;
		}

		default:
			break;
	}
	return ::DefWindowProc(_hSelf, message, wParam, lParam);
}

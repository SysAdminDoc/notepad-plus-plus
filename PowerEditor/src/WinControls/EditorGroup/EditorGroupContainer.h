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

#pragma once

#include <windows.h>
#include <vector>

#include "EditorGroup.h"
#include "Window.h"

#define WM_EDITORGROUP_SPLITTER_MOVED (WM_APP + 0x100)
#define WM_EDITORGROUP_DEFERRED_REMOVE (WM_APP + 0x101)

class EditorGroupContainer : public Window
{
public:
	EditorGroupContainer() = default;
	~EditorGroupContainer() override = default;

	void create(HINSTANCE hInst, HWND parent);
	void destroy() override;
	void reSizeTo(RECT& rc) override;
	void display(bool toShow = true) const override;
	void redraw(bool forceUpdate = false) const override;

	int addGroup(const EditorGroup& group, bool skipLayout = false);
	void removeGroup(int index);
	int groupCount() const { return static_cast<int>(_groups.size()); }

	EditorGroup& getGroup(int index) { return _groups[index]; }
	const EditorGroup& getGroup(int index) const { return _groups[index]; }

	EditorGroup* getGroupById(int id);
	int getGroupIndexById(int id) const;
	int getGroupIndexByEditView(const ScintillaEditView* view) const;
	int getGroupIndexByDocTab(const DocTabView* tab) const;
	int getGroupIndexByHwnd(HWND hwnd) const;

	DropZoneInfo hitTestDropZone(POINT screenPt) const;
	void showDropOverlay(const DropZoneInfo& info);
	void hideDropOverlay();

	RECT getGroupRect(int index) const;

	void setActiveGroup(int index) { _activeGroupIndex = index; }
	int activeGroupIndex() const { return _activeGroupIndex; }

	int splitterSize() const { return _splitterGap; }

	void setMinColumnWidth(int w) { _minColumnWidth = w; }
	int scrollOffset() const { return _scrollOffset; }

private:
	std::vector<EditorGroup> _groups;
	int _activeGroupIndex = 0;
	int _splitterGap = 4;
	int _minColumnWidth = 200;
	int _scrollOffset = 0;
	int _totalContentWidth = 0;

	bool _isDraggingSplitter = false;
	int _dragSplitterIndex = -1;
	int _hoverSplitterIndex = -1;
	bool _isTrackingMouseLeave = false;
	POINT _dragStartPoint = {};
	double _dragStartRatioLeft = 0.0;
	double _dragStartRatioRight = 0.0;

	HWND _hDropOverlay = nullptr;
	bool _dropOverlayVisible = false;

	RECT _lastRect = {};
	std::vector<RECT> _groupRects;
	std::vector<RECT> _splitterRects;

	static bool _isRegistered;
	static LRESULT CALLBACK staticWinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT runProc(UINT message, WPARAM wParam, LPARAM lParam);

	void recalcLayout();
	void applyLayout() const;
	int splitterHitTest(POINT clientPt) const;
	void invalidateSplitter(int index) const;
	void createDropOverlay();
	void normalizeRatios();
	void updateScrollBar();
};

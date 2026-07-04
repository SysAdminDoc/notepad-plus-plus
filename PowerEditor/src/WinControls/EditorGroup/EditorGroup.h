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

class DocTabView;
class ScintillaEditView;
class AutoCompletion;

struct EditorGroup
{
	int id = -1;
	DocTabView* docTab = nullptr;
	ScintillaEditView* editView = nullptr;
	AutoCompletion* autoComplete = nullptr;
	bool isDynamic = false;
	double widthRatio = 1.0;

	bool isValid() const { return docTab != nullptr && editView != nullptr; }
	bool isDestroyed() const { return docTab == nullptr && editView == nullptr && isDynamic; }
};

enum class DropPosition
{
	None,
	Center,
	Left,
	Right
};

struct DropZoneInfo
{
	int groupIndex = -1;
	DropPosition position = DropPosition::None;
	RECT overlayRect = {};
};

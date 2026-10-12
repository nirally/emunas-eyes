#pragma once
#include <windows.h>

namespace mecvr::window_title {

// Finds the game's main window (visible, owned by this process). nullptr if not up yet.
HWND find_game_window();

// Appends a tag to the game window title, as visible proof the mod is running.
// Returns false if the window doesn't exist yet.
bool tag(const wchar_t* text);

}  // namespace mecvr::window_title

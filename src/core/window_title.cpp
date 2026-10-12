#include "core/window_title.h"

#include <cwchar>
#include <string>

namespace mecvr::window_title {

HWND find_game_window() {
    struct Ctx { DWORD pid; HWND found; } ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != c->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            wchar_t title[256];
            if (GetWindowTextW(hwnd, title, 256) && wcsstr(title, L"Catalyst")) {
                c->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool tag(const wchar_t* text) {
    HWND hwnd = find_game_window();
    if (!hwnd) return false;
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    if (wcsstr(title, text)) return true;
    std::wstring next = std::wstring(title) + L"  " + text;
    return SetWindowTextW(hwnd, next.c_str()) != FALSE;
}

}  // namespace mecvr::window_title

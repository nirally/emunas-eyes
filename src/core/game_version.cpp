#include "core/game_version.h"

#include <windows.h>

#include <cstdio>
#include <vector>

namespace mecvr::game_version {

std::string host_exe_version() {
    // We may *be* version.dll, so call the real one explicitly.
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    wcscat_s(sys + n, MAX_PATH - n, L"\\version.dll");
    HMODULE ver = LoadLibraryW(sys);
    if (!ver) return {};

    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto size_fn = reinterpret_cast<SizeFn>(GetProcAddress(ver, "GetFileVersionInfoSizeW"));
    auto info_fn = reinterpret_cast<InfoFn>(GetProcAddress(ver, "GetFileVersionInfoW"));
    auto query_fn = reinterpret_cast<QueryFn>(GetProcAddress(ver, "VerQueryValueW"));
    if (!size_fn || !info_fn || !query_fn) return {};

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    DWORD handle = 0;
    DWORD size = size_fn(exe, &handle);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!info_fn(exe, 0, size, data.data())) return {};

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!query_fn(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) || !ffi) return {};
    char buf[64];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
             HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    return buf;
}

}  // namespace mecvr::game_version

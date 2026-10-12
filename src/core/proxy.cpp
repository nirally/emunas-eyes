// Proxy-DLL forwarding. The game loads us as version.dll (or dinput8.dll); every
// export jumps straight to the real system DLL through g_real (see proxy_stubs.asm),
// so signatures never need to be known here.
#include "core/proxy.h"

#include <cwchar>

extern "C" {
// Order must match proxy_stubs.asm.
void* g_real[23] = {};
}

namespace mecvr::proxy {
namespace {

constexpr const char* kVersionExports[] = {
    "GetFileVersionInfoA",      "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",    "GetFileVersionInfoSizeA",    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",   "GetFileVersionInfoW",
    "VerFindFileA",             "VerFindFileW",               "VerInstallFileA",
    "VerInstallFileW",          "VerLanguageNameA",           "VerLanguageNameW",
    "VerQueryValueA",           "VerQueryValueW"};
constexpr int kVersionFirst = 0;

constexpr const char* kDInput8Exports[] = {"DirectInput8Create", "DllCanUnloadNow",     "DllGetClassObject",
                                           "DllRegisterServer",  "DllUnregisterServer", "GetdfDIJoystick"};
constexpr int kDInput8First = 17;

// Any export whose real DLL we didn't load. Returning 0 is safe for every
// signature on x64 (caller cleans the stack) and reads as "failed".
unsigned long long unresolved_export() { return 0; }

void fill(const wchar_t* dll, const char* const* names, int count, int first) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    swprintf_s(path + n, MAX_PATH - n, L"\\%s", dll);
    HMODULE real = LoadLibraryW(path);
    for (int i = 0; i < count; ++i) {
        FARPROC fn = real ? GetProcAddress(real, names[i]) : nullptr;
        g_real[first + i] = fn ? reinterpret_cast<void*>(fn) : reinterpret_cast<void*>(&unresolved_export);
    }
}

}  // namespace

Role init(HMODULE self) {
    for (int i = 0; i < 23; ++i) g_real[i] = reinterpret_cast<void*>(&unresolved_export);

    wchar_t me[MAX_PATH];
    GetModuleFileNameW(self, me, MAX_PATH);
    const wchar_t* name = wcsrchr(me, L'\\');
    name = name ? name + 1 : me;

    if (_wcsicmp(name, L"version.dll") == 0) {
        fill(L"version.dll", kVersionExports, 17, kVersionFirst);
        return Role::Version;
    }
    if (_wcsicmp(name, L"dinput8.dll") == 0) {
        fill(L"dinput8.dll", kDInput8Exports, 6, kDInput8First);
        return Role::DInput8;
    }
    return Role::Unknown;
}

}  // namespace mecvr::proxy

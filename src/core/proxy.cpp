// Proxy-DLL forwarding. The game loads us in place of one DLL (decided by our file
// name); every export bumps a call counter and jumps straight to the real DLL
// through g_real (see proxy_stubs.asm), so signatures never need to be known here.
#include "core/proxy.h"

#include <cwchar>

extern "C" {
// Order must match proxy_stubs.asm.
void* g_real[mecvr::proxy::kExportCount] = {};
volatile unsigned long long g_calls[mecvr::proxy::kExportCount] = {};
}

namespace mecvr::proxy {
namespace {

constexpr const char* kExportNames[kExportCount] = {
    // version.dll (0-16)
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW",
    // dinput8.dll (17-22)
    "DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer", "DllUnregisterServer",
    "GetdfDIJoystick",
    // NvCameraSDK64.dll (23-31)
    "getConfiguration", "getConfigurationSize", "getSessionConfigurationSize", "getVersion", "setConfiguration",
    "setStopSessionCallback", "setUpdateCameraFunc", "stopSession", "updateCamera"};

struct Group { int first, count; };
constexpr Group kVersion{0, 17}, kDInput8{17, 6}, kNvCamera{23, 9};

Role g_role = Role::Unknown;

// Any export whose real DLL we didn't load. Returning 0 is safe for every
// signature on x64 (caller cleans the stack) and reads as "failed".
unsigned long long unresolved_export() { return 0; }

bool fill(const wchar_t* full_path, Group g) {
    HMODULE real = LoadLibraryW(full_path);
    for (int i = g.first; i < g.first + g.count; ++i) {
        FARPROC fn = real ? GetProcAddress(real, kExportNames[i]) : nullptr;
        if (fn) g_real[i] = reinterpret_cast<void*>(fn);
    }
    return real != nullptr;
}

bool fill_system(const wchar_t* dll, Group g) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    swprintf_s(path + n, MAX_PATH - n, L"\\%s", dll);
    return fill(path, g);
}

}  // namespace

Role init(HMODULE self) {
    for (auto& p : g_real) p = reinterpret_cast<void*>(&unresolved_export);

    wchar_t me[MAX_PATH];
    GetModuleFileNameW(self, me, MAX_PATH);
    wchar_t* slash = wcsrchr(me, L'\\');
    const wchar_t* name = slash ? slash + 1 : me;

    if (_wcsicmp(name, L"version.dll") == 0) {
        fill_system(L"version.dll", kVersion);
        g_role = Role::Version;
    } else if (_wcsicmp(name, L"dinput8.dll") == 0) {
        fill_system(L"dinput8.dll", kDInput8);
        g_role = Role::DInput8;
    } else if (_wcsicmp(name, L"NvCameraSDK64.dll") == 0) {
        // The installer moved the game's original into mecvr\.
        if (slash) *slash = 0;
        wchar_t orig[MAX_PATH];
        swprintf_s(orig, L"%s\\mecvr\\NvCameraSDK64_orig.dll", me);
        g_role = fill(orig, kNvCamera) ? Role::NvCamera : Role::NvCameraMissingOriginal;
    }
    return g_role;
}

const char* export_name(int index) { return (index >= 0 && index < kExportCount) ? kExportNames[index] : "?"; }
unsigned long long call_count(int index) { return (index >= 0 && index < kExportCount) ? g_calls[index] : 0; }

}  // namespace mecvr::proxy

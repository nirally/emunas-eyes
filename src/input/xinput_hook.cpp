// XInput hooks for the virtual pad (D-026). See xinput_hook.h.
#include "input/xinput_hook.h"

#include <windows.h>
#include <Xinput.h>
#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <filesystem>
#include <string>

#include <MinHook.h>

#include "diagnostics/log.h"

namespace mecvr::xinput_hook {
namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using GetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);

constexpr const wchar_t* kDlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll",
                                    L"xinput1_1.dll"};
constexpr int kDllCount = sizeof(kDlls) / sizeof(kDlls[0]);
enum Entry { kGetState, kGetStateEx, kSetState, kGetCaps, kEntryCount };
const char* const kEntryNames[kEntryCount] = {"XInputGetState", "XInputGetStateEx(#100)", "XInputSetState",
                                              "XInputGetCapabilities"};

// One trampoline per (dll, entry); detours are generated per dll index by the template below.
GetStateFn g_get[kDllCount], g_get_ex[kDllCount];
SetStateFn g_set[kDllCount];
GetCapsFn g_caps[kDllCount];
bool g_hooked[kDllCount];

std::atomic<bool> g_virtual{false};
std::mutex g_pad_mutex;
Pad g_pad;
DWORD g_packet = 0;
std::atomic<uint32_t> g_vibration{0};  // left << 16 | right

// Stats: calls per (dll, entry, user index 0..3), and one caller per entry.
std::atomic<uint64_t> g_calls[kDllCount][kEntryCount][4];
std::atomic<uintptr_t> g_caller[kDllCount][kEntryCount];
std::atomic<uint32_t> g_last_result[kDllCount][4];

void note(int dll, int entry, DWORD user, void* ra) {
    if (user < 4) ++g_calls[dll][entry][user];
    g_caller[dll][entry] = reinterpret_cast<uintptr_t>(ra);
}

bool serve(DWORD user, XINPUT_STATE* st) {
    if (!g_virtual.load() || user != 0 || !st) return false;
    std::lock_guard lock(g_pad_mutex);
    st->dwPacketNumber = ++g_packet;
    st->Gamepad.wButtons = g_pad.buttons;
    st->Gamepad.bLeftTrigger = g_pad.left_trigger;
    st->Gamepad.bRightTrigger = g_pad.right_trigger;
    st->Gamepad.sThumbLX = g_pad.lx;
    st->Gamepad.sThumbLY = g_pad.ly;
    st->Gamepad.sThumbRX = g_pad.rx;
    st->Gamepad.sThumbRY = g_pad.ry;
    return true;
}

template <int D>
DWORD WINAPI get_state(DWORD user, XINPUT_STATE* st) {
    note(D, kGetState, user, _ReturnAddress());
    if (serve(user, st)) return ERROR_SUCCESS;
    const DWORD r = g_get[D](user, st);
    if (user < 4) g_last_result[D][user] = r;
    return r;
}
template <int D>
DWORD WINAPI get_state_ex(DWORD user, XINPUT_STATE* st) {
    note(D, kGetStateEx, user, _ReturnAddress());
    if (serve(user, st)) return ERROR_SUCCESS;
    return g_get_ex[D](user, st);
}
template <int D>
DWORD WINAPI set_state(DWORD user, XINPUT_VIBRATION* v) {
    note(D, kSetState, user, _ReturnAddress());
    if (user == 0 && v) g_vibration = (uint32_t(v->wLeftMotorSpeed) << 16) | v->wRightMotorSpeed;
    if (g_virtual.load() && user == 0) return ERROR_SUCCESS;
    return g_set[D](user, v);
}
template <int D>
DWORD WINAPI get_caps(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
    note(D, kGetCaps, user, _ReturnAddress());
    if (g_virtual.load() && user == 0 && caps) {
        *caps = {};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Flags = XINPUT_CAPS_FFB_SUPPORTED;  // claim rumble support (games may skip pads without it)
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = -64;
        caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = 0xFFFF;
        return ERROR_SUCCESS;
    }
    return g_caps[D](user, flags, caps);
}

template <int D>
void hook_dll(HMODULE m) {
    auto hook = [&](void* target, void* detour, void** orig, const char* what) {
        if (!target) return;
        MH_STATUS s = MH_CreateHook(target, detour, orig);
        if (s == MH_OK) s = MH_EnableHook(target);
        if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED)
            LOG_W("input", "hooking %s in %ls failed: %s", what, kDlls[D], MH_StatusToString(s));
    };
    hook(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState")), reinterpret_cast<void*>(&get_state<D>),
         reinterpret_cast<void**>(&g_get[D]), "XInputGetState");
    hook(reinterpret_cast<void*>(GetProcAddress(m, reinterpret_cast<LPCSTR>(100))),
         reinterpret_cast<void*>(&get_state_ex<D>), reinterpret_cast<void**>(&g_get_ex[D]), "XInputGetStateEx");
    hook(reinterpret_cast<void*>(GetProcAddress(m, "XInputSetState")), reinterpret_cast<void*>(&set_state<D>),
         reinterpret_cast<void**>(&g_set[D]), "XInputSetState");
    hook(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetCapabilities")), reinterpret_cast<void*>(&get_caps<D>),
         reinterpret_cast<void**>(&g_caps[D]), "XInputGetCapabilities");
    LOG_I("input", "XInput hooks installed in %ls", kDlls[D]);
}

template <int D>
void try_dll() {
    if (g_hooked[D]) return;
    if (HMODULE m = GetModuleHandleW(kDlls[D])) {
        g_hooked[D] = true;
        hook_dll<D>(m);
    }
}

std::string where(uintptr_t a) {
    HMODULE m = nullptr;
    if (!a || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(a), &m))
        return "?";
    char path[MAX_PATH];
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = strrchr(path, '\\');
    char out[300];
    snprintf(out, sizeof(out), "%s+%llx", m == GetModuleHandleW(nullptr) ? "exe" : (base ? base + 1 : path),
             (unsigned long long)(a - reinterpret_cast<uintptr_t>(m)));
    return out;
}

}  // namespace

void poll_install() {
    try_dll<0>();
    try_dll<1>();
    try_dll<2>();
    try_dll<3>();
    try_dll<4>();
}

void set_virtual(bool active) {
    g_virtual = active;
    LOG_I("input", "virtual pad %s", active ? "ON (controller 0)" : "OFF");
}

void set_pad(const Pad& pad) {
    std::lock_guard lock(g_pad_mutex);
    g_pad = pad;
}

void last_vibration(uint16_t& left_motor, uint16_t& right_motor) {
    const uint32_t v = g_vibration.load();
    left_motor = uint16_t(v >> 16);
    right_motor = uint16_t(v & 0xffff);
}

void report() {
    for (int d = 0; d < kDllCount; ++d) {
        if (!g_hooked[d]) continue;
        for (int e = 0; e < kEntryCount; ++e) {
            uint64_t c[4];
            for (int u = 0; u < 4; ++u) c[u] = g_calls[d][e][u].exchange(0);
            if (!(c[0] | c[1] | c[2] | c[3])) continue;
            LOG_I("input", "%ls %s: pad0 %llu pad1 %llu pad2 %llu pad3 %llu calls /10 s (from %s); last result pad0 %lu",
                  kDlls[d], kEntryNames[e], (unsigned long long)c[0], (unsigned long long)c[1], (unsigned long long)c[2],
                  (unsigned long long)c[3], where(g_caller[d][e].load()).c_str(), (unsigned long)g_last_result[d][0].load());
        }
    }
}

}  // namespace mecvr::xinput_hook

namespace mecvr::xinput_hook {
// PADTEST file + padtest.txt: lines "buttons(hex) lx ly rx ry lt rt ms" played in order on the
// virtual pad, then the virtual pad goes back to its previous on/off state. Developer test.
void poll_test(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::remove(dir / L"PADTEST", ec)) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (dir / L"padtest.txt").c_str(), L"r") != 0 || !f) return;
    const bool was = g_virtual.load();
    g_virtual = true;
    char line[160];
    int steps = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned buttons = 0;
        int lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0, ms = 0;
        if (sscanf_s(line, "%x %d %d %d %d %d %d %d", &buttons, &lx, &ly, &rx, &ry, &lt, &rt, &ms) != 8) continue;
        Pad p;
        p.buttons = uint16_t(buttons);
        p.lx = int16_t(lx); p.ly = int16_t(ly); p.rx = int16_t(rx); p.ry = int16_t(ry);
        p.left_trigger = uint8_t(lt); p.right_trigger = uint8_t(rt);
        set_pad(p);
        LOG_I("input", "pad test: buttons %04x L(%d,%d) R(%d,%d) T(%d,%d) for %d ms", buttons, lx, ly, rx, ry, lt, rt, ms);
        Sleep(ms);
        ++steps;
    }
    fclose(f);
    set_pad(Pad{});
    Sleep(100);
    g_virtual = was;
    LOG_I("input", "pad test done (%d steps)", steps);
}
}  // namespace mecvr::xinput_hook

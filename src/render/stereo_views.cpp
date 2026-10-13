// Stage 2 experiment H1 (STAGE2-RECON.md): give Frostbite's split-screen view builder two
// views for player 0 - the real view object and a copy - each with half of the viewport,
// only for the duration of the builder call. No engine allocation: the view list is
// pointed at our own two-entry array and restored right after.
#include "render/stereo_views.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include <MinHook.h>

#include "core/sigscan.h"
#include "diagnostics/log.h"

namespace mecvr::stereo_views {
namespace {

using BuilderFn = void(__fastcall*)(void*, void*, void*, void*);

constexpr const char* kBuilderSig =
    "55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 A0 EC FF FF B8 60 14 00 00 E8 ?? ?? ?? ?? 48 29 C4 "
    "48 C7 85 90 00 00 00 FE FF FF FF 48 89 9C 24 B0 14 00 00";
constexpr uintptr_t kViewListVectorRva = 0x2415e60;  // static vector object: begin +8, end +0x10

BuilderFn g_orig = nullptr;
uintptr_t g_exe = 0;
std::atomic<bool> g_enabled{false};
std::atomic<uint64_t> g_split_frames{0};

// Variant B (D-017): the same view object twice in the list (a byte copy corrupted the engine's
// intrusive lists - crash at exe+0x2c7fa3f, a list unlink). Its viewport is switched to the left
// or right half right before each FillRenderView, and restored after the builder.
uintptr_t g_ptrs[2];
std::atomic<bool> g_in_builder{false};
int g_fill_index = 0;
uint8_t* g_obj = nullptr;
uint32_t g_saved_x = 0;
uint16_t g_saved_w = 0, g_half = 0;
float g_saved_aspect = 0, g_half_aspect = 0;

using FillFn = void(__fastcall*)(void*, void*, void*, void*);
FillFn g_fill_orig = nullptr;
// In the builder: mov rdx,rbx; mov rcx,rdi; call FillRenderView; movaps xmm0,[rbx]; movaps [rbx+0xfd0],xmm0
constexpr const char* kFillCallSig = "48 89 DA 48 89 F9 E8 ?? ?? ?? ?? 0F 28 03 0F 29 83 D0 0F 00 00";

void __fastcall fill_detour(void* obj, void* rv, void* c, void* d) {
    if (g_in_builder.load() && obj == g_obj) {
        int eye = g_fill_index++ & 1;
        *reinterpret_cast<uint32_t*>(g_obj + 0x10) = g_saved_x + (eye ? g_half : 0);
        *reinterpret_cast<uint16_t*>(g_obj + 0x1c) = g_half;
        *reinterpret_cast<float*>(g_obj + 0x20) = g_half_aspect;
    }
    g_fill_orig(obj, rv, c, d);
}

void __fastcall builder_detour(void* a, void* b, void* c, void* d) {
    if (!g_enabled.load()) { g_orig(a, b, c, d); return; }
    auto* vec = reinterpret_cast<uintptr_t*>(g_exe + kViewListVectorRva);
    uintptr_t begin = vec[1], end = vec[2];
    if (!begin || end - begin != 8) { g_orig(a, b, c, d); return; }
    auto* obj = reinterpret_cast<uint8_t*>(*reinterpret_cast<uintptr_t*>(begin));
    if (!obj) { g_orig(a, b, c, d); return; }
    uint16_t w = *reinterpret_cast<uint16_t*>(obj + 0x1c), h = *reinterpret_cast<uint16_t*>(obj + 0x1e);
    if (w < 64 || h < 64) { g_orig(a, b, c, d); return; }

    g_obj = obj;
    g_saved_x = *reinterpret_cast<uint32_t*>(obj + 0x10);
    g_saved_w = w;
    g_saved_aspect = *reinterpret_cast<float*>(obj + 0x20);
    g_half = static_cast<uint16_t>(w / 2);
    g_half_aspect = static_cast<float>(g_half) / h;
    g_fill_index = 0;

    g_ptrs[0] = g_ptrs[1] = reinterpret_cast<uintptr_t>(obj);
    vec[1] = reinterpret_cast<uintptr_t>(&g_ptrs[0]);
    vec[2] = reinterpret_cast<uintptr_t>(&g_ptrs[2]);
    g_in_builder = true;

    g_orig(a, b, c, d);

    g_in_builder = false;
    vec[1] = begin;
    vec[2] = end;
    *reinterpret_cast<uint32_t*>(obj + 0x10) = g_saved_x;
    *reinterpret_cast<uint16_t*>(obj + 0x1c) = g_saved_w;
    *reinterpret_cast<float*>(obj + 0x20) = g_saved_aspect;
    if (g_fill_index == 2) ++g_split_frames;
}

bool hook_fill() {
    auto pat = sigscan::parse(kFillCallSig);
    const uint8_t* site = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *pat);
    if (!site) { LOG_E("stereo", "FillRenderView call site not found"); return false; }
    auto* target = const_cast<uint8_t*>(sigscan::rip_relative(site + 6, 1, 5));
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&fill_detour), reinterpret_cast<void**>(&g_fill_orig));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) { LOG_E("stereo", "FillRenderView hook failed: %s", MH_StatusToString(st)); return false; }
    LOG_I("stereo", "FillRenderView hooked at exe+%llx", (unsigned long long)(reinterpret_cast<uintptr_t>(target) - g_exe));
    return true;
}

}  // namespace

bool install() {
    g_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto pat = sigscan::parse(kBuilderSig);
    const uint8_t* fn = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *pat);
    if (!fn) {
        LOG_E("stereo", "view builder signature not found");
        return false;
    }
    MH_STATUS st = MH_CreateHook(const_cast<uint8_t*>(fn), reinterpret_cast<void*>(&builder_detour),
                                 reinterpret_cast<void**>(&g_orig));
    if (st == MH_OK) st = MH_EnableHook(const_cast<uint8_t*>(fn));
    if (st != MH_OK) {
        LOG_E("stereo", "view builder hook failed: %s", MH_StatusToString(st));
        return false;
    }
    if (!hook_fill()) return false;
    LOG_I("stereo", "view builder hooked at exe+%llx (experiment %s)", (unsigned long long)(reinterpret_cast<uintptr_t>(fn) - g_exe),
          g_enabled.load() ? "ON" : "off");
    return true;
}

void set_enabled(bool on) {
    if (g_enabled.exchange(on) != on) LOG_I("stereo", "two-view experiment %s", on ? "ON" : "OFF");
}

uint64_t split_frames() { return g_split_frames.load(); }

}  // namespace mecvr::stereo_views

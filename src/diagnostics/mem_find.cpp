// Recon helpers (Stage 2, per-eye TAA), polled from the main loop:
//   MEMFIND - takes the latest unpatched per-view constant buffer and searches all writable memory for
//             exact copies of its previous view-projection row, current view-projection row and the
//             previous/current camera positions; writes memfind_<n>.txt with each hit's address,
//             region and the floats around it (to find the engine structure the constants come from).
//   WATCH   - reads a hex address from watch.txt and records which code writes it for 1.5 s
//             (debug-register watch, hwbp) to watch_<address>.txt.
#include "diagnostics/mem_find.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "diagnostics/hwbp.h"
#include "diagnostics/log.h"
#include "render/cb_capture.h"
#include "render/hand_render.h"
#include "render/taa_eyes.h"

namespace mecvr::mem_find {
namespace {

struct Key {
    const char* name;
    unsigned first;  // float index in the view CB
    unsigned count;
};
constexpr Key kKeys[] = {
    {"previous view-projection row 0", 40, 4},
    {"current view-projection row 0", 72, 4},
    {"previous camera position", 184, 3},
    {"current camera position", 180, 3},
};

bool safe_copy(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void dump_around(FILE* f, uintptr_t at) {
    float around[48];
    if (!safe_copy(reinterpret_cast<const void*>(at - 64), around, sizeof(around))) return;
    for (int i = 0; i < 48; ++i)
        fprintf(f, "%s%+6d:%11.5f", i % 8 ? " " : "\n      ", (i - 16) * 4, around[i]);
    fprintf(f, "\n");
}

void find(const std::filesystem::path& dir) {
    float cb[284];
    if (!cb_capture::last_view_cb(cb)) {
        LOG_W("recon", "memfind: no view constants seen yet");
        return;
    }
    static int index = 0;
    wchar_t name[32];
    swprintf_s(name, L"memfind_%d.txt", ++index);
    FILE* f = nullptr;
    if (_wfopen_s(&f, (dir / name).c_str(), L"w") != 0 || !f) return;
    const HMODULE self = GetModuleHandleW(L"NvCameraSDK64.dll");
    for (const Key& k : kKeys) {
        fprintf(f, "\n== %s (view CB floats %u..%u):", k.name, k.first, k.first + k.count - 1);
        for (unsigned i = 0; i < k.count; ++i) fprintf(f, " %.6f", cb[k.first + i]);
        fprintf(f, "\n");
        const size_t bytes = k.count * 4;
        int hits = 0;
        MEMORY_BASIC_INFORMATION mbi;
        std::vector<uint8_t> buf;
        for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi));
             a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            const DWORD rw = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE;
            if (mbi.State != MEM_COMMIT || !(mbi.Protect & rw) || (mbi.Protect & PAGE_GUARD)) continue;
            buf.resize(mbi.RegionSize);
            if (!safe_copy(mbi.BaseAddress, buf.data(), buf.size())) continue;
            for (size_t o = 0; o + bytes <= buf.size(); o += 4) {
                if (memcmp(buf.data() + o, &cb[k.first], bytes) != 0) continue;
                const uintptr_t at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + o;
                HMODULE owner = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(at), &owner);
                fprintf(f, "  hit %p  region %p size %zx type %s%s", reinterpret_cast<void*>(at), mbi.BaseAddress,
                        (size_t)mbi.RegionSize, mbi.Type == MEM_PRIVATE ? "private" : mbi.Type == MEM_MAPPED ? "mapped" : "image",
                        owner == self ? " (mod)" : owner ? " (module)" : "");
                dump_around(f, at);
                if (++hits >= 40) break;
            }
            if (hits >= 40) break;
        }
        fprintf(f, "  %d hits\n", hits);
    }
    fclose(f);
    LOG_I("recon", "memfind %d written", index);
}

}  // namespace

void poll(const std::filesystem::path& dir) {
    std::error_code ec;
    if (std::filesystem::remove(dir / L"VELOCITY", ec)) taa_eyes::probe_velocity_next();
    if (std::filesystem::remove(dir / L"STENCIL", ec)) hand_render::stencil_probe_next(dir);
    if (std::filesystem::remove(dir / L"MEMFIND", ec)) find(dir);
    if (std::filesystem::remove(dir / L"WATCH", ec)) {
        std::ifstream in(dir / L"watch.txt");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const uintptr_t at = std::stoull(line, nullptr, 16);
            char label[48];
            snprintf(label, sizeof(label), "watch %llx", (unsigned long long)at);
            wchar_t name[48];
            swprintf_s(name, L"watch_%llx.txt", (unsigned long long)at);
            hwbp::trace_writes(at, 1500, dir / name, label);
            LOG_I("recon", "write watch on %llx done", (unsigned long long)at);
        }
    }
}

}  // namespace mecvr::mem_find

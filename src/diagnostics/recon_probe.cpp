// Recon helper: searches all committed readable memory in the process for name
// strings, region by region, and reports where they live. Read-only.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "diagnostics/log.h"

namespace mecvr::recon {
namespace {

// Returns hits; *faulted set if the region threw partway.
size_t scan(uintptr_t begin, uintptr_t end, const char* s, size_t n, uintptr_t* first, bool* faulted) {
    size_t hits = 0;
    __try {
        for (uintptr_t p = begin; p + n <= end; ++p)
            if (*reinterpret_cast<const char*>(p) == s[0] && memcmp(reinterpret_cast<const void*>(p), s, n) == 0) {
                if (!hits) *first = p;
                ++hits;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = true; }
    return hits;
}

}  // namespace

void probe_strings() {
    const char* probes[] = {"d3d11.dll", "GameRenderer", "RenderView", "ClientGameContext", "FaithCharacter",
                            "CameraComponent", "Frostbite"};
    auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    uintptr_t exe_end = exe + nt->OptionalHeader.SizeOfImage;

    for (const char* s : probes) {
        size_t n = strlen(s);
        size_t in_exe = 0, in_other = 0, regions = 0, faults = 0;
        uintptr_t first_exe = 0, first_other = 0;
        uintptr_t cur = 0x10000;
        MEMORY_BASIC_INFORMATION mbi{};
        while (VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) {
            uintptr_t b = reinterpret_cast<uintptr_t>(mbi.BaseAddress), e = b + mbi.RegionSize;
            bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && mbi.Protect != 0;
            if (ok && mbi.RegionSize < (512ull << 20)) {
                ++regions;
                bool faulted = false;
                uintptr_t first = 0;
                size_t h = scan(b, e, s, n, &first, &faulted);
                if (faulted) ++faults;
                if (b >= exe && b < exe_end) { if (h && !first_exe) first_exe = first; in_exe += h; }
                else { if (h && !first_other) first_other = first; in_other += h; }
            }
            if (e <= cur) break;
            cur = e;
        }
        LOG_I("recon", "probe '%s': exe %zu (first %llx), other %zu (first %llx); %zu regions, %zu faulted", s, in_exe,
              (unsigned long long)first_exe, in_other, (unsigned long long)first_other, regions, faults);
    }
}

}  // namespace mecvr::recon

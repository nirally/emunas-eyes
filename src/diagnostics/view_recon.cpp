// Stage 2 recon (read-only). When a VIEWRECON file appears in the mod folder, logs the
// view builder's inputs (docs/recon/STAGE2-RECON.md): the camera/view object list at
// [exe+0x2415e50] and the object whose vfunc +0x68 gives the local view count, and
// writes the code of that vfunc to viewcount_fn.bin for disassembly.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "diagnostics/log.h"

namespace mecvr::view_recon {
namespace {

constexpr uintptr_t kViewListRva = 0x2415e50;    // from 0x31a421b: mov rcx, [rip-0xd8e3d2]
constexpr uintptr_t kViewCountObjRva = 0x2563300; // from 0x31a4208: mov rcx, [rip-0xc40f0f]

template <typename T>
bool rd(uintptr_t p, T& out) {
    __try { out = *reinterpret_cast<const T*>(p); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool copy(uintptr_t p, void* out, size_t n) {
    __try { memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}  // namespace

void poll(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::remove(dir / L"VIEWRECON", ec)) return;
    auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    uintptr_t list = 0, begin = 0, end = 0;
    rd(exe + kViewListRva, list);
    if (list) { rd(list + 8, begin); rd(list + 0x10, end); }
    size_t n = (begin && end >= begin) ? (end - begin) / 8 : 0;
    LOG_I("viewrecon", "view list %llx: %zu entries", (unsigned long long)list, n);
    for (size_t i = 0; i < n && i < 32; ++i) {
        uintptr_t obj = 0, vt = 0, q18 = 0;
        uint32_t i10 = 0, i14 = 0, idx = 0;
        uint8_t active = 0;
        float f20 = 0;
        rd(begin + i * 8, obj);
        if (!obj) { LOG_I("viewrecon", "  [%zu] null", i); continue; }
        rd(obj, vt); rd(obj + 0x10, i10); rd(obj + 0x14, i14); rd(obj + 0x18, q18); rd(obj + 0x20, f20);
        rd(obj + 0x28, idx); rd(obj + 0x1b8f, active);
        LOG_I("viewrecon", "  [%zu] obj %llx vtable exe+%llx  +10=%u +14=%u +18=%llx +20=%.4f view_index(+28)=%u active(+1b8f)=%u", i,
              (unsigned long long)obj, (unsigned long long)(vt - exe), i10, i14, (unsigned long long)q18, f20, idx, active);
    }

    uintptr_t cobj = 0, cvt = 0, fn = 0;
    rd(exe + kViewCountObjRva, cobj);
    if (cobj) rd(cobj, cvt);
    if (cvt) rd(cvt + 0x68, fn);
    LOG_I("viewrecon", "view-count object %llx vtable exe+%llx vfunc+0x68 exe+%llx", (unsigned long long)cobj,
          (unsigned long long)(cvt ? cvt - exe : 0), (unsigned long long)(fn ? fn - exe : 0));
    if (fn) {
        uint8_t code[256];
        if (copy(fn, code, sizeof(code))) {
            std::ofstream out(dir / L"viewcount_fn.bin", std::ios::binary);
            out.write(reinterpret_cast<const char*>(code), sizeof(code));
        }
    }
    // Raw dump of the view-count object's first 0x100 bytes for layout analysis.
    if (cobj) {
        uint8_t raw[0x100];
        if (copy(cobj, raw, sizeof(raw))) {
            std::ofstream out(dir / L"viewcount_obj.bin", std::ios::binary);
            out.write(reinterpret_cast<const char*>(raw), sizeof(raw));
        }
    }
}

}  // namespace mecvr::view_recon

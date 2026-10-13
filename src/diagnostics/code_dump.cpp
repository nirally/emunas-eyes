// Recon helper: when a CODEDUMP file appears in the mod folder, reads RVAs (hex,
// one per line) from codedump.txt, finds each one's enclosing function via the
// exe's unwind table, and writes the function bytes to codedump_<rva>.bin plus
// an index line (function begin/end RVA). Read-only.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "diagnostics/log.h"

namespace mecvr::code_dump {
namespace {

bool copy_bytes(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}  // namespace

void poll(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::remove(dir / L"CODEDUMP", ec)) return;
    auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::ifstream in(dir / L"codedump.txt");
    FILE* idx = nullptr;
    _wfopen_s(&idx, (dir / L"codedump_index.txt").c_str(), L"w");
    std::string line;
    int done = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        uintptr_t rva = std::stoull(line, nullptr, 16);
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(exe + rva, &image_base, nullptr);
        uintptr_t begin, end;
        if (fn) {
            // Chained unwind info: walk to the primary entry for the real start.
            begin = fn->BeginAddress;
            end = fn->EndAddress;
        } else {
            begin = rva > 0x200 ? rva - 0x200 : 0;
            end = rva + 0x100;
        }
        size_t n = end - begin;
        if (n > 0x10000) n = 0x10000;
        std::string buf(n, '\0');
        bool ok = copy_bytes(reinterpret_cast<const void*>(exe + begin), buf.data(), n);
        char name[64];
        snprintf(name, sizeof(name), "codedump_%llx.bin", (unsigned long long)rva);
        if (ok) {
            std::ofstream out(dir / name, std::ios::binary);
            out.write(buf.data(), static_cast<std::streamsize>(n));
        }
        if (idx)
            fprintf(idx, "%llx in function %llx..%llx (%s, %zu bytes) -> %s\n", (unsigned long long)rva,
                    (unsigned long long)begin, (unsigned long long)end, fn ? "unwind entry" : "no unwind entry, window",
                    n, ok ? name : "UNREADABLE");
        ++done;
    }
    if (idx) fclose(idx);
    LOG_I("recon", "code dump: %d functions written", done);
}

}  // namespace mecvr::code_dump

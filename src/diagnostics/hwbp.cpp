#include "diagnostics/hwbp.h"

#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::hwbp {
namespace {

constexpr int kMaxFrames = 10;
constexpr int kMaxSites = 64;

struct Site {
    uintptr_t frames[kMaxFrames];
    int depth;
    unsigned hits;
};

std::atomic<uintptr_t> g_target{0};
Site g_sites[kMaxSites];
std::atomic<int> g_site_count{0};
std::atomic<unsigned> g_total_hits{0};

// Unwind from the faulting context. No allocation, no locks: runs inside the VEH.
int capture(CONTEXT* ctx, uintptr_t* out) {
    CONTEXT c = *ctx;
    int n = 0;
    while (n < kMaxFrames && c.Rip) {
        out[n++] = c.Rip;
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
        if (!fn) {  // leaf function: return address is at [rsp]
            c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
            c.Rsp += 8;
        } else {
            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, fn, &c, &handler_data, &establisher, nullptr);
        }
    }
    return n;
}

LONG CALLBACK veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* ctx = ep->ContextRecord;
    if (!(ctx->Dr6 & 1) || !g_target.load()) return EXCEPTION_CONTINUE_SEARCH;
    ctx->Dr6 = 0;
    ++g_total_hits;

    uintptr_t frames[kMaxFrames];
    int depth = 0;
    __try { depth = capture(ctx, frames); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (depth == 0) { frames[0] = ctx->Rip; depth = 1; }

    int count = g_site_count.load();
    for (int i = 0; i < count; ++i) {
        if (g_sites[i].frames[0] == frames[0] && g_sites[i].depth > 1 == depth > 1 &&
            (depth < 2 || g_sites[i].frames[1] == frames[1])) {
            ++g_sites[i].hits;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    int slot = g_site_count.fetch_add(1);
    if (slot < kMaxSites) {
        Site& s = g_sites[slot];
        for (int i = 0; i < depth; ++i) s.frames[i] = frames[i];
        s.depth = depth;
        s.hits = 1;
    } else {
        g_site_count = kMaxSites;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Applies DR0/DR7 to every thread in the process except the caller.
int set_all_threads(uintptr_t address, bool on) {
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{sizeof(te)};
    int applied = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != static_cast<DWORD>(-1)) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                if (on) {
                    c.Dr0 = address;
                    // L0 enable, RW0 = 01 (write), LEN0 = 11 (4 bytes)
                    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);
                } else {
                    c.Dr0 = 0;
                    c.Dr7 &= ~0xF0003ull;
                }
                if (SetThreadContext(t, &c)) ++applied;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    return applied;
}

std::string describe(uintptr_t addr) {
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod)) {
        GetModuleFileNameA(mod, name, MAX_PATH);
        const char* base = strrchr(name, '\\');
        char buf[MAX_PATH + 32];
        snprintf(buf, sizeof(buf), "%s+%llx", base ? base + 1 : name,
                 static_cast<unsigned long long>(addr - reinterpret_cast<uintptr_t>(mod)));
        return buf;
    }
    snprintf(name, sizeof(name), "%llx", static_cast<unsigned long long>(addr));
    return name;
}

}  // namespace

void trace_writes(uintptr_t address, unsigned ms, const std::filesystem::path& out, const char* label) {
    g_site_count = 0;
    g_total_hits = 0;
    g_target = address;
    PVOID handler = AddVectoredExceptionHandler(1, veh);
    int threads = set_all_threads(address, true);
    LOG_I("hwbp", "watching writes to %llx (%s) on %d threads for %u ms", (unsigned long long)address, label, threads, ms);
    Sleep(ms);
    set_all_threads(address, false);
    Sleep(50);  // let in-flight exceptions drain
    g_target = 0;
    RemoveVectoredExceptionHandler(handler);

    int count = g_site_count.load();
    LOG_I("hwbp", "%s: %u writes from %d distinct sites", label, g_total_hits.load(), count);
    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"a") != 0 || !f) return;
    fprintf(f, "\n## writes to %llx (%s): %u hits, %d sites, watch on %d threads for %u ms\n",
            (unsigned long long)address, label, g_total_hits.load(), count, threads, ms);
    for (int i = 0; i < count; ++i) {
        fprintf(f, "site %d (%u hits):\n", i, g_sites[i].hits);
        for (int d = 0; d < g_sites[i].depth; ++d) fprintf(f, "   %s\n", describe(g_sites[i].frames[d]).c_str());
    }
    fclose(f);
}

}  // namespace mecvr::hwbp

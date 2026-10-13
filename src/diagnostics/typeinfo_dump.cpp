// Frostbite keeps a linked list of TypeInfo objects (one per reflected class).
// Layout varies per game, so this doesn't assume it: it finds the "GameRenderer"
// name string, then pointers to that string (the type's info data), then pointers
// to those (the TypeInfo), and probes nearby slots for a "next" pointer that leads
// to more TypeInfos with valid names.
#include "diagnostics/typeinfo_dump.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::typeinfo_dump {
namespace {

struct Region { uintptr_t begin, end; };
std::vector<Region> g_regions;

void map_module() {
    g_regions.clear();
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    uintptr_t cur = base, end = base + nt->OptionalHeader.SizeOfImage;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) break;
        uintptr_t rend = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (readable) {
            if (!g_regions.empty() && g_regions.back().end == cur) g_regions.back().end = rend;
            else g_regions.push_back({cur, rend});
        }
        cur = rend;
    }
}

bool readable(uintptr_t p, size_t n) {
    for (auto& r : g_regions)
        if (p >= r.begin && p + n <= r.end) return true;
    return false;
}

bool read_u64(uintptr_t p, uint64_t& out) {
    if (!readable(p, 8)) return false;
    __try { out = *reinterpret_cast<const uint64_t*>(p); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A plausible class name: 2-96 chars of [A-Za-z0-9_:<>], NUL-terminated.
bool read_name(uintptr_t p, std::string& out) {
    out.clear();
    for (int i = 0; i < 97; ++i) {
        if (!readable(p + i, 1)) return false;
        char c;
        __try { c = *reinterpret_cast<const char*>(p + i); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (c == 0) return i >= 2;
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '<' || c == '>')) return false;
        out.push_back(c);
    }
    return false;
}

// Raw scanners: no C++ objects in these, so SEH guards are allowed.
size_t scan_string(uintptr_t begin, uintptr_t end, const char* s, size_t n, uintptr_t* out, size_t cap) {
    size_t found = 0;
    __try {
        for (uintptr_t p = begin + 1; p + n <= end && found < cap; ++p)
            if (*reinterpret_cast<const char*>(p - 1) == 0 && *reinterpret_cast<const char*>(p) == s[0] &&
                memcmp(reinterpret_cast<const void*>(p), s, n) == 0)
                out[found++] = p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return found;
}

size_t scan_qword(uintptr_t begin, uintptr_t end, uint64_t target, uintptr_t* out, size_t cap) {
    size_t found = 0;
    __try {
        for (uintptr_t p = (begin + 7) & ~uintptr_t(7); p + 8 <= end && found < cap; p += 8)
            if (*reinterpret_cast<const uint64_t*>(p) == target) out[found++] = p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return found;
}

std::vector<uintptr_t> find_string(const char* s) {
    std::vector<uintptr_t> hits;
    uintptr_t buf[64];
    for (auto& r : g_regions) {
        size_t n = scan_string(r.begin, r.end, s, strlen(s) + 1, buf, 64);
        hits.insert(hits.end(), buf, buf + n);
    }
    return hits;
}

std::vector<uintptr_t> find_pointers_to(uintptr_t target) {
    std::vector<uintptr_t> hits;
    uintptr_t buf[64];
    for (auto& r : g_regions) {
        size_t n = scan_qword(r.begin, r.end, target, buf, 64);
        hits.insert(hits.end(), buf, buf + n);
    }
    return hits;
}

void hexdump(FILE* f, uintptr_t p, size_t n) {
    for (size_t off = 0; off < n; off += 8) {
        uint64_t v = 0;
        if (!read_u64(p + off, v)) { fprintf(f, "    +%03zx  <unreadable>\n", off); break; }
        std::string s;
        bool named = read_name(static_cast<uintptr_t>(v), s);
        fprintf(f, "    +%03zx  %016llx%s%s\n", off, static_cast<unsigned long long>(v), named ? "  -> \"" : "",
                named ? (s + "\"").c_str() : "");
    }
}

// Given TypeInfo candidate `ti` with info-data at ti+info_off, name at info+name_off,
// and next pointer at ti+next_off: walk the list and return names.
std::vector<std::pair<uintptr_t, std::string>> walk(uintptr_t ti, int info_off, int name_off, int next_off,
                                                    size_t limit) {
    std::vector<std::pair<uintptr_t, std::string>> out;
    std::set<uintptr_t> seen;
    while (ti && out.size() < limit && seen.insert(ti).second) {
        uint64_t info = 0, name_ptr = 0, next = 0;
        std::string name;
        if (!read_u64(ti + info_off, info) || !read_u64(info + name_off, name_ptr) ||
            !read_name(static_cast<uintptr_t>(name_ptr), name))
            break;
        out.emplace_back(ti, name);
        if (!read_u64(ti + next_off, next)) break;
        ti = static_cast<uintptr_t>(next);
    }
    return out;
}

const char* kInteresting[] = {"Renderer", "RenderView", "Camera", "View", "Faith", "Soldier", "HeadBob",
                              "Shake", "Fov", "Player", "Character", "Input", "Hud", "UI"};

}  // namespace

void run(const std::filesystem::path& out_path) {
    map_module();
    size_t total = 0;
    for (auto& r : g_regions) total += r.end - r.begin;
    LOG_I("recon", "typeinfo: %zu readable regions, %zu MB of %zu MB image", g_regions.size(), total >> 20,
          static_cast<size_t>(reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + reinterpret_cast<IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr))->e_lfanew)->OptionalHeader.SizeOfImage) >> 20);
    for (const char* probe : {"GameRenderer", "RenderView", "ClientGameContext", "FaithCharacter", "CameraComponent", "TypeInfo"})
        LOG_I("recon", "typeinfo: probe '%s' -> %zu hits", probe, find_string(probe).size());

    FILE* f = nullptr;
    if (_wfopen_s(&f, out_path.c_str(), L"w") != 0 || !f) { LOG_E("recon", "typeinfo: cannot open output"); return; }

    auto strings = find_string("GameRenderer");
    fprintf(f, "# \"GameRenderer\" strings: %zu\n", strings.size());
    LOG_I("recon", "typeinfo: %zu 'GameRenderer' strings", strings.size());

    // Try each string -> info -> typeinfo chain with a few common layouts; keep the longest walk.
    std::vector<std::pair<uintptr_t, std::string>> best;
    int best_cfg[3] = {};
    for (uintptr_t s : strings) {
        for (uintptr_t info_slot : find_pointers_to(s)) {
            for (int name_off : {0, 8}) {
                uintptr_t info = info_slot - name_off;
                for (uintptr_t ti_slot : find_pointers_to(info)) {
                    fprintf(f, "# candidate: string %llx  info %llx (name at +%d)  ref at %llx\n",
                            (unsigned long long)s, (unsigned long long)info, name_off, (unsigned long long)ti_slot);
                    for (int info_off : {8, 0, 16}) {
                        uintptr_t ti = ti_slot - info_off;
                        for (int next_off : {16, 8, 24, 32}) {
                            if (next_off == info_off) continue;
                            auto w = walk(ti, info_off, name_off, next_off, 200000);
                            if (w.size() > best.size()) {
                                best = std::move(w);
                                best_cfg[0] = info_off; best_cfg[1] = name_off; best_cfg[2] = next_off;
                            }
                        }
                    }
                }
            }
        }
    }

    fprintf(f, "# best walk: %zu types (info at TI+%d, name at info+%d, next at TI+%d)\n", best.size(), best_cfg[0],
            best_cfg[1], best_cfg[2]);
    LOG_I("recon", "typeinfo: best walk %zu types (info+%d name+%d next+%d)", best.size(), best_cfg[0], best_cfg[1],
          best_cfg[2]);

    // The walk starts at GameRenderer, which may sit mid-list. Sweep the whole module
    // with the discovered layout to catch the types before it.
    if (best.size() > 50) {
        std::set<uintptr_t> have;
        for (auto& e : best) have.insert(e.first);
        size_t added = 0;
        auto is_ti = [&](uintptr_t a, std::string& name) {
            uint64_t info = 0, np = 0, next = 0;
            if (!read_u64(a + best_cfg[0], info) || !read_u64(info + best_cfg[1], np) ||
                !read_name(static_cast<uintptr_t>(np), name) || !read_u64(a + best_cfg[2], next))
                return false;
            if (next == 0) return true;
            uint64_t ninfo = 0, nnp = 0;
            std::string nname;
            return read_u64(next + best_cfg[0], ninfo) && read_u64(ninfo + best_cfg[1], nnp) &&
                   read_name(static_cast<uintptr_t>(nnp), nname);
        };
        for (auto& r : g_regions) {
            for (uintptr_t a = (r.begin + 7) & ~uintptr_t(7); a + 0x40 <= r.end; a += 8) {
                if (have.count(a)) continue;
                std::string name;
                if (is_ti(a, name)) { best.emplace_back(a, name); have.insert(a); ++added; }
            }
        }
        fprintf(f, "# sweep added %zu more types (total %zu)\n", added, best.size());
        LOG_I("recon", "typeinfo: sweep added %zu (total %zu)", added, best.size());
    }

    for (auto& [ti, name] : best) {
        uint64_t info = 0;
        read_u64(ti + best_cfg[0], info);
        fprintf(f, "%llx\t%llx\t%s\n", (unsigned long long)ti, (unsigned long long)info, name.c_str());
    }

    fprintf(f, "\n# ---- raw dumps of interesting types (TypeInfo, then info data) ----\n");
    int dumped = 0;
    for (auto& [ti, name] : best) {
        bool hit = false;
        for (auto* k : kInteresting) if (name.find(k) != std::string::npos) { hit = true; break; }
        if (!hit || dumped >= 400) continue;
        ++dumped;
        uint64_t info = 0;
        read_u64(ti + best_cfg[0], info);
        fprintf(f, "\n## %s  TI=%llx\n", name.c_str(), (unsigned long long)ti);
        hexdump(f, ti, 0x40);
        fprintf(f, "  info=%llx\n", (unsigned long long)info);
        hexdump(f, static_cast<uintptr_t>(info), 0x60);
    }
    fclose(f);
    LOG_I("recon", "typeinfo: done, %d interesting types dumped", dumped);
}

}  // namespace mecvr::typeinfo_dump

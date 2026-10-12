#include "core/sigscan.h"

#include <windows.h>

#include <cctype>
#include <string>

namespace mecvr::sigscan {

std::optional<Pattern> parse(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') { ++i; continue; }
        if (text[i] == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
            continue;
        }
        if (i + 1 >= text.size() || !isxdigit(static_cast<unsigned char>(text[i])) ||
            !isxdigit(static_cast<unsigned char>(text[i + 1])))
            return std::nullopt;
        p.bytes.push_back(static_cast<uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
        p.mask.push_back(true);
        i += 2;
    }
    if (p.bytes.empty()) return std::nullopt;
    return p;
}

const uint8_t* find(const uint8_t* data, size_t size, const Pattern& p) {
    const size_t n = p.bytes.size();
    if (n == 0 || size < n) return nullptr;
    for (size_t i = 0; i + n <= size; ++i) {
        size_t j = 0;
        while (j < n && (!p.mask[j] || data[i + j] == p.bytes[j])) ++j;
        if (j == n) return data + i;
    }
    return nullptr;
}

const uint8_t* find_in_module(void* module_base, const Pattern& p) {
    auto base = static_cast<const uint8_t*>(module_base);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        // Packed sections can be partly unmapped; walk them region by region.
        const uint8_t* cur = base + section->VirtualAddress;
        const uint8_t* end = cur + section->Misc.VirtualSize;
        while (cur < end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(cur, &mbi, sizeof(mbi))) break;
            const uint8_t* region_end = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
            if (region_end > end) region_end = end;
            bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
            if (readable) {
                if (auto hit = find(cur, static_cast<size_t>(region_end - cur), p)) return hit;
            }
            cur = region_end;
        }
    }
    return nullptr;
}

}  // namespace mecvr::sigscan

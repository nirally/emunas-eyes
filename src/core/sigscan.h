#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace mecvr::sigscan {

// "48 8B 05 ?? ?? ?? ??" -> bytes + mask. Returns nullopt on a malformed pattern.
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;  // true = must match
};
std::optional<Pattern> parse(std::string_view text);

// First match in [data, data+size), or nullptr.
const uint8_t* find(const uint8_t* data, size_t size, const Pattern& p);

// Scans every readable, executable section of a loaded module.
const uint8_t* find_in_module(void* module_base, const Pattern& p);

// Resolves a RIP-relative operand: address of instruction + offset to disp32 + instruction length.
inline const uint8_t* rip_relative(const uint8_t* insn, size_t disp_offset, size_t insn_len) {
    int32_t disp = *reinterpret_cast<const int32_t*>(insn + disp_offset);
    return insn + insn_len + disp;
}

}  // namespace mecvr::sigscan

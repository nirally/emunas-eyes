#pragma once
#include <filesystem>

namespace mecvr::typeinfo_dump {

// Recon tool (Stage 1). Finds Frostbite's reflection list starting from a known
// class-name string and writes every type name, plus raw bytes of interesting
// types, to `out`. Read-only; every read is bounds-checked against the main module.
void run(const std::filesystem::path& out);

}  // namespace mecvr::typeinfo_dump

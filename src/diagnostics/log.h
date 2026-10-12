#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace mecvr::log {

enum class Level { Error = 0, Warn, Info, Debug, Trace };

// Opens the log file (truncates). Safe to call once; later calls are ignored.
void init(const std::filesystem::path& file);
void set_level(Level level);
Level level_from_string(const std::string& s, Level fallback);

void write(Level level, const char* tag, const char* fmt, ...);

// Last N lines, for crash dumps and the in-VR bug report.
std::vector<std::string> tail(size_t max_lines = 500);

}  // namespace mecvr::log

#define LOG_E(tag, ...) ::mecvr::log::write(::mecvr::log::Level::Error, tag, __VA_ARGS__)
#define LOG_W(tag, ...) ::mecvr::log::write(::mecvr::log::Level::Warn, tag, __VA_ARGS__)
#define LOG_I(tag, ...) ::mecvr::log::write(::mecvr::log::Level::Info, tag, __VA_ARGS__)
#define LOG_D(tag, ...) ::mecvr::log::write(::mecvr::log::Level::Debug, tag, __VA_ARGS__)
#define LOG_T(tag, ...) ::mecvr::log::write(::mecvr::log::Level::Trace, tag, __VA_ARGS__)

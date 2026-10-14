#include "diagnostics/log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace mecvr::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::atomic<Level> g_level{Level::Info};
std::deque<std::string> g_ring;
constexpr size_t kRingSize = 500;

const char* level_name(Level l) {
    switch (l) {
        case Level::Error: return "ERROR";
        case Level::Warn:  return "WARN ";
        case Level::Info:  return "INFO ";
        case Level::Debug: return "DEBUG";
        case Level::Trace: return "TRACE";
    }
    return "?????";
}

}  // namespace

void init(const std::filesystem::path& file) {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    g_file = _wfsopen(file.c_str(), L"w", _SH_DENYWR);
    if (!g_file) {
        // Another process holding the mod already has the main log open (PLAYTEST-2c: a second process
        // loaded us and logged nothing) - write to a per-process file instead of going silent.
        std::filesystem::path alt = file;
        alt.replace_filename(file.stem().wstring() + L"." + std::to_wstring(GetCurrentProcessId()) +
                             file.extension().wstring());
        g_file = _wfsopen(alt.c_str(), L"w", _SH_DENYWR);
    }
}

void set_level(Level level) { g_level = level; }

Level level_from_string(const std::string& s, Level fallback) {
    if (s == "error") return Level::Error;
    if (s == "warn") return Level::Warn;
    if (s == "info") return Level::Info;
    if (s == "debug") return Level::Debug;
    if (s == "trace") return Level::Trace;
    return fallback;
}

void write(Level level, const char* tag, const char* fmt, ...) {
    if (level > g_level.load()) return;

    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[2200];
    snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d [%s] [%-8s] %s", t.wHour, t.wMinute, t.wSecond,
             t.wMilliseconds, level_name(level), tag, msg);

    std::lock_guard lock(g_mutex);
    g_ring.emplace_back(line);
    if (g_ring.size() > kRingSize) g_ring.pop_front();
    if (g_file) {
        fputs(line, g_file);
        fputc('\n', g_file);
        fflush(g_file);
    }
}

std::vector<std::string> tail(size_t max_lines) {
    std::lock_guard lock(g_mutex);
    size_t start = g_ring.size() > max_lines ? g_ring.size() - max_lines : 0;
    return {g_ring.begin() + start, g_ring.end()};
}

}  // namespace mecvr::log

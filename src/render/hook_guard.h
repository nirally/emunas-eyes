#pragma once
// Device-context hooks exist once per version of each method (ctx_hooks: the runtime switches the
// context between method tables, BUG-011), and one version can call another (the thread-safe version
// locks, then calls the plain one). Only the outermost hooked call does the mod's work; calls made inside
// it - including the mod's own draws - pass straight through.

namespace mecvr::hook_guard {

inline thread_local int g_depth = 0;

struct Inside {
    bool nested;
    Inside() : nested(g_depth++ > 0) {}
    ~Inside() { --g_depth; }
    Inside(const Inside&) = delete;
    Inside& operator=(const Inside&) = delete;
};

}  // namespace mecvr::hook_guard

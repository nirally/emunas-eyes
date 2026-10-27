#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mecvr::config {

using json = nlohmann::json;

// Pure: resolve user values against the schema. Every schema key ends up in the
// result. Bad values fall back to the default (or clamp) and add a warning;
// a typo never crashes the game.
json resolve(const json& schema, const json& user, std::vector<std::string>& warnings);

// Loads schema + user config from disk and keeps them current.
class Store {
public:
    bool load(const std::filesystem::path& schema_file, const std::filesystem::path& user_file);
    // Re-reads the user file if it changed on disk. Returns true if values changed.
    bool poll_reload();

    // Writes one value into the user file (config.json) and applies it. Main thread only.
    void set_user_value(const std::string& key, const json& value);
    // Several at once (the settings menu's reset), one write.
    void set_user_values(const json& values);

    bool get_bool(const std::string& key) const;
    double get_number(const std::string& key) const;
    std::string get_string(const std::string& key) const;
    const json& values() const { return values_; }

    void on_change(std::function<void(const json& old_values, const json& new_values)> cb) {
        callbacks_.push_back(std::move(cb));
    }

private:
    json read_user_file() const;

    std::filesystem::path schema_file_, user_file_;
    std::filesystem::file_time_type user_mtime_{};
    json schema_ = json::object();
    json values_ = json::object();
    std::vector<std::function<void(const json&, const json&)>> callbacks_;
};

}  // namespace mecvr::config

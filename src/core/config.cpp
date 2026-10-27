#include "core/config.h"

#include <algorithm>
#include <fstream>

#include "diagnostics/log.h"

namespace mecvr::config {

json resolve(const json& schema, const json& user, std::vector<std::string>& warnings) {
    json out = json::object();
    const json& settings = schema.contains("settings") ? schema["settings"] : json::object();

    for (auto& [key, def] : settings.items()) {
        const json& fallback = def["default"];
        const std::string type = def.value("type", "");
        if (!user.is_object() || !user.contains(key)) {
            out[key] = fallback;
            continue;
        }
        const json& v = user[key];

        if (type == "bool") {
            if (v.is_boolean()) out[key] = v;
            else { warnings.push_back(key + ": expected true/false, using default"); out[key] = fallback; }
        } else if (type == "float" || type == "int") {
            if (!v.is_number()) {
                warnings.push_back(key + ": expected a number, using default");
                out[key] = fallback;
                continue;
            }
            double d = v.get<double>();
            double lo = def.value("min", d), hi = def.value("max", d);
            double clamped = std::clamp(d, lo, hi);
            if (clamped != d) warnings.push_back(key + ": out of range, clamped");
            if (type == "int") out[key] = static_cast<long long>(clamped);
            else out[key] = clamped;
        } else if (type == "enum") {
            const json& allowed = def["values"];
            if (v.is_string() && std::find(allowed.begin(), allowed.end(), v) != allowed.end()) out[key] = v;
            else { warnings.push_back(key + ": not one of the allowed values, using default"); out[key] = fallback; }
        } else {
            if (v.type() == fallback.type()) out[key] = v;
            else { warnings.push_back(key + ": wrong type, using default"); out[key] = fallback; }
        }
    }

    if (user.is_object()) {
        for (auto& [key, _] : user.items()) {
            if (key.rfind("$", 0) == 0) continue;
            if (!settings.contains(key)) warnings.push_back(key + ": unknown setting, ignored");
        }
    }
    return out;
}

static json read_json(const std::filesystem::path& file, bool& ok) {
    ok = false;
    std::ifstream in(file);
    if (!in) return json::object();
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (j.is_discarded()) return json::object();
    ok = true;
    return j;
}

json Store::read_user_file() const {
    bool ok = false;
    json j = read_json(user_file_, ok);
    if (!ok && std::filesystem::exists(user_file_))
        LOG_W("config", "config.json could not be parsed - using defaults for everything");
    return j;
}

bool Store::load(const std::filesystem::path& schema_file, const std::filesystem::path& user_file) {
    schema_file_ = schema_file;
    user_file_ = user_file;
    bool ok = false;
    schema_ = read_json(schema_file, ok);
    if (!ok) {
        LOG_E("config", "schema.json missing or invalid at %s", schema_file.string().c_str());
        return false;
    }
    std::error_code ec;
    user_mtime_ = std::filesystem::last_write_time(user_file_, ec);

    std::vector<std::string> warnings;
    values_ = resolve(schema_, read_user_file(), warnings);
    for (auto& w : warnings) LOG_W("config", "%s", w.c_str());
    LOG_I("config", "loaded %zu settings (%zu warnings)", values_.size(), warnings.size());
    return true;
}

bool Store::poll_reload() {
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(user_file_, ec);
    if (ec || mtime == user_mtime_) return false;
    user_mtime_ = mtime;

    std::vector<std::string> warnings;
    json next = resolve(schema_, read_user_file(), warnings);
    for (auto& w : warnings) LOG_W("config", "%s", w.c_str());
    if (next == values_) return false;

    for (auto& [key, v] : next.items())
        if (values_[key] != v) LOG_I("config", "hot reload: %s = %s", key.c_str(), v.dump().c_str());
    json old = std::move(values_);
    values_ = std::move(next);
    for (auto& cb : callbacks_) cb(old, values_);
    return true;
}

bool Store::get_bool(const std::string& key) const {
    auto it = values_.find(key);
    return it != values_.end() && it->is_boolean() && it->get<bool>();
}

double Store::get_number(const std::string& key) const {
    auto it = values_.find(key);
    return (it != values_.end() && it->is_number()) ? it->get<double>() : 0.0;
}

std::string Store::get_string(const std::string& key) const {
    auto it = values_.find(key);
    return (it != values_.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

}  // namespace mecvr::config

namespace mecvr::config {

void Store::set_user_value(const std::string& key, const json& value) {
    json one = json::object();
    one[key] = value;
    set_user_values(one);
}

void Store::set_user_values(const json& values) {
    json user = read_user_file();
    if (!user.is_object()) user = json::object();
    for (auto& [k, v] : values.items()) user[k] = v;
    {
        std::ofstream out(user_file_);
        out << user.dump(2) << "\n";
    }
    user_mtime_ = {};  // two writes within the file time's resolution must not look like no change
    poll_reload();     // re-validate and notify like any other edit
}

}  // namespace mecvr::config

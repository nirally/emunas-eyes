#include <filesystem>
#include <fstream>

#include "core/config.h"
#include "tests/check.h"

using mecvr::config::json;
using mecvr::config::resolve;

static json test_schema() {
    return json::parse(R"({ "settings": {
        "a.flag":  { "type": "bool",  "default": true },
        "a.speed": { "type": "float", "min": 30, "max": 360, "default": 120 },
        "a.count": { "type": "int",   "min": 1, "max": 5, "default": 2 },
        "a.mode":  { "type": "enum",  "values": ["smooth", "snap"], "default": "smooth" }
    }})");
}

TEST(config_defaults_when_user_empty) {
    std::vector<std::string> w;
    json v = resolve(test_schema(), json::object(), w);
    CHECK(v["a.flag"] == true);
    CHECK(v["a.speed"] == 120);
    CHECK(v["a.mode"] == "smooth");
    CHECK(w.empty());
}

TEST(config_valid_values_pass_through) {
    std::vector<std::string> w;
    json v = resolve(test_schema(), json::parse(R"({"a.flag": false, "a.speed": 200, "a.mode": "snap"})"), w);
    CHECK(v["a.flag"] == false);
    CHECK(v["a.speed"] == 200.0);
    CHECK(v["a.mode"] == "snap");
    CHECK(w.empty());
}

TEST(config_bad_values_fall_back_with_warning) {
    std::vector<std::string> w;
    json v = resolve(test_schema(), json::parse(R"({"a.flag": "yes", "a.mode": "teleport", "a.speed": "fast"})"), w);
    CHECK(v["a.flag"] == true);
    CHECK(v["a.mode"] == "smooth");
    CHECK(v["a.speed"] == 120);
    CHECK(w.size() == 3);
}

TEST(config_out_of_range_clamps) {
    std::vector<std::string> w;
    json v = resolve(test_schema(), json::parse(R"({"a.speed": 9000, "a.count": 0})"), w);
    CHECK(v["a.speed"] == 360.0);
    CHECK(v["a.count"] == 1);
    CHECK(w.size() == 2);
}

TEST(config_unknown_key_warns) {
    std::vector<std::string> w;
    resolve(test_schema(), json::parse(R"({"a.typo": 1, "$comment": "x"})"), w);
    CHECK(w.size() == 1);
}

TEST(config_project_schema_is_valid) {
    // The shipped schema must parse and every entry needs a type, default and menu section.
    std::ifstream in("config/schema.json");
    CHECK(in.good());
    json s = json::parse(in, nullptr, false);
    CHECK(!s.is_discarded());
    for (auto& [key, def] : s["settings"].items()) {
        CHECK(def.contains("type"));
        CHECK(def.contains("default"));
        CHECK(def.contains("section"));
    }
    std::vector<std::string> w;
    resolve(s, json::object(), w);
    CHECK(w.empty());
}

TEST(config_set_user_value_persists_and_applies) {
    auto dir = std::filesystem::temp_directory_path() / "mecvr_test_config";
    std::filesystem::create_directories(dir);
    {
        std::ofstream s(dir / "schema.json");
        s << test_schema().dump();
        std::ofstream u(dir / "config.json");
        u << R"({"a.flag": false})";
    }
    mecvr::config::Store store;
    CHECK(store.load(dir / "schema.json", dir / "config.json"));
    int changes = 0;
    store.on_change([&](const json&, const json&) { ++changes; });
    store.set_user_value("a.speed", 200.0);
    CHECK(store.get_number("a.speed") == 200.0);
    CHECK(store.get_bool("a.flag") == false);  // other user values kept
    CHECK(changes == 1);
    {
        std::ifstream in(dir / "config.json");
        json saved = json::parse(in);
        CHECK(saved["a.speed"] == 200.0 && saved["a.flag"] == false);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

#include "core/settings_menu.h"
#include "tests/check.h"

#include <fstream>

using namespace mecvr;
using settings_menu::Key;
using json = nlohmann::json;

namespace {

json small_schema() {
    return json::parse(R"({"settings": {
        "c.on":    {"type": "bool",  "default": true,  "section": "Comfort", "label": "On thing"},
        "c.speed": {"type": "float", "min": 0, "max": 2, "default": 1, "unit": "m/s", "section": "Comfort", "label": "Speed"},
        "u.mode":  {"type": "enum",  "values": ["a", "b", "c"], "default": "a", "section": "UI", "label": "Mode"},
        "d.log":   {"type": "bool",  "default": false, "section": "Diagnostics", "label": "Log"}
    }})");
}

json defaults(const json& schema) {
    json v = json::object();
    for (auto& [k, e] : schema["settings"].items()) v[k] = e["default"];
    return v;
}

void apply_changes(json& v, const std::vector<settings_menu::Change>& c) {
    for (const auto& x : c) v[x.key] = x.value;
}

}  // namespace

TEST(settings_menu_has_every_schema_setting) {
    // The shipped schema: every setting is a menu item, in a section the menu lists.
    std::ifstream in("config/schema.json");
    const json s = json::parse(in, nullptr, false);
    CHECK(!s.is_discarded());
    const auto items = settings_menu::items_from_schema(s);
    CHECK(items.size() == s["settings"].size());
    settings_menu::Menu m(items);
    size_t listed = 0;
    for (int p = 0; p < int(m.sections().size()); ++p) {
        bool close = false;
        m.reset_view();
        for (int k = 0; k < p; ++k) m.press(Key::Down, json::object(), &close);
        m.press(Key::Ok, json::object(), &close);
        listed += m.lines(json::object()).size() - 2;  // minus reset and back
    }
    CHECK(listed == items.size());
}

TEST(settings_menu_sections_in_order_with_diagnostics_last) {
    settings_menu::Menu m(settings_menu::items_from_schema(small_schema()));
    CHECK(m.sections().size() == 3);
    CHECK(m.sections()[0] == "Comfort" && m.sections()[1] == "UI" && m.sections()[2] == "Diagnostics");
}

TEST(settings_menu_changes_values_by_type) {
    const json s = small_schema();
    json v = defaults(s);
    settings_menu::Menu m(settings_menu::items_from_schema(s));
    bool close = false;
    m.press(Key::Ok, v, &close);  // open Comfort
    CHECK(m.page() == 0);
    apply_changes(v, m.press(Key::Right, v, &close));  // the bool toggles
    CHECK(v["c.on"] == false);
    m.press(Key::Down, v, &close);
    for (int i = 0; i < 100; ++i) apply_changes(v, m.press(Key::Right, v, &close));  // the float clamps at max
    CHECK(v["c.speed"].get<double>() == 2.0);
    apply_changes(v, m.press(Key::Left, v, &close));
    CHECK(std::fabs(v["c.speed"].get<double>() - 1.95) < 1e-9);  // step 0.05 over a 0-2 range
    CHECK(m.lines(v)[1].value == "1.95 m/s");
    m.press(Key::Back, v, &close);
    CHECK(m.page() == -1 && !close);
    m.press(Key::Down, v, &close);
    m.press(Key::Ok, v, &close);  // UI
    apply_changes(v, m.press(Key::Left, v, &close));  // enum wraps backwards
    CHECK(v["u.mode"] == "c");
}

TEST(settings_menu_reset_asks_twice) {
    const json s = small_schema();
    json v = defaults(s);
    v["c.on"] = false;
    v["c.speed"] = 0.5;
    settings_menu::Menu m(settings_menu::items_from_schema(s));
    bool close = false;
    m.press(Key::Ok, v, &close);
    m.press(Key::Down, v, &close);
    m.press(Key::Down, v, &close);  // "Reset this section"
    CHECK(m.press(Key::Ok, v, &close).empty());  // asks first
    CHECK(m.lines(v)[2].text.find("again") != std::string::npos);
    const auto c = m.press(Key::Ok, v, &close);
    CHECK(c.size() == 2);
    apply_changes(v, c);
    CHECK(v["c.on"] == true && v["c.speed"] == 1);
    // Moving away cancels a pending reset.
    v["c.on"] = false;
    m.press(Key::Ok, v, &close);
    m.press(Key::Up, v, &close);
    m.press(Key::Down, v, &close);
    CHECK(m.press(Key::Ok, v, &close).empty());
}

TEST(settings_menu_closes_from_the_section_list) {
    settings_menu::Menu m(settings_menu::items_from_schema(small_schema()));
    bool close = false;
    m.press(Key::Back, json::object(), &close);
    CHECK(close);
    m.reset_view();
    m.press(Key::Up, json::object(), &close);  // wraps to "Close"
    m.press(Key::Ok, json::object(), &close);
    CHECK(close);
}

TEST(settings_menu_nice_steps) {
    CHECK(settings_menu::nice_step(690) == 20);  // turn speed 30-720
    CHECK(std::fabs(settings_menu::nice_step(1.3) - 0.02) < 1e-12);  // height offset -0.5-0.8: 2 cm, like F2/F3
    CHECK(std::fabs(settings_menu::nice_step(1) - 0.02) < 1e-12);
}

TEST(settings_menu_keeps_the_schema_files_order) {
    const auto s = nlohmann::ordered_json::parse(R"({"settings": {
        "z.first":  {"type": "bool", "default": true, "section": "Hands", "label": "First"},
        "a.second": {"type": "bool", "default": true, "section": "Hands", "label": "Second"}
    }})");
    const auto items = settings_menu::items_from_schema(s);
    CHECK(items.size() == 2 && items[0].key == "z.first" && items[1].key == "a.second");
}

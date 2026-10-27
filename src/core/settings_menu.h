#pragma once
// Stage 9: the in-VR settings menu's model (pure, unit-tested). Every setting comes from config/schema.json - its
// type (bool / enum / float), range, default, section and label - so the menu and the config can't drift apart.
// Pages: the sections, then one section's settings. Sticks move and change, A picks, B goes back; resets ask for a
// second A. The menu hands back changes (key, value); the caller writes them (config::Store::set_user_value).
// 9B: profiles - kSlots saved copies of your settings (a household sharing a headset switches with one pick). Loading
// one is ordinary changes (every setting to the profile's value, or its default when the profile doesn't set it);
// saving hands the slot and the settings that differ from their defaults to the caller, which writes the file.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mecvr::settings_menu {

using json = nlohmann::json;

struct Item {
    std::string key, type, label, unit, section;
    double min = 0, max = 1, step = 0.1;
    bool restart = false;  // only read when the game starts
    std::vector<std::string> values;  // enum
    json def;
};

// A round step giving ~40 positions over the range (1, 2 or 5 times a power of ten).
inline double nice_step(double range) {
    if (!(range > 0)) return 0.1;
    const double raw = range / 40, p = std::pow(10.0, std::floor(std::log10(raw))), m = raw / p;
    return p * (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10);
}

// The sections in menu order (most used first); sections the schema has that aren't listed follow, then Diagnostics.
inline const std::vector<std::string>& section_order() {
    static const std::vector<std::string> o = {"Comfort", "Controls", "Body", "Hands", "Combat", "UI", "Rendering", "Calibration", "Input", "Features"};
    return o;
}

// J = nlohmann::json or nlohmann::ordered_json (the menu lists a section's settings in the schema file's order).
template <class J>
std::vector<Item> items_from_schema(const J& schema) {
    std::vector<Item> out;
    const J& s = schema.contains("settings") ? schema["settings"] : schema;
    for (auto it = s.begin(); it != s.end(); ++it) {
        const J& e = it.value();
        if (!e.is_object() || !e.contains("type")) continue;
        Item i;
        i.key = it.key();
        i.type = e["type"].template get<std::string>();
        i.label = e.value("label", i.key);
        i.unit = e.value("unit", "");
        i.section = e.value("section", "Other");
        i.def = e.contains("default") ? json::parse(e["default"].dump()) : json();
        i.restart = e.value("restart", false);
        if (i.type == "float") {
            i.min = e.value("min", 0.0);
            i.max = e.value("max", 1.0);
            i.step = e.value("step", nice_step(i.max - i.min));
        }
        if (i.type == "enum" && e.contains("values"))
            for (const auto& v : e["values"]) i.values.push_back(v.template get<std::string>());
        if (i.type == "bool" || i.type == "float" || (i.type == "enum" && !i.values.empty())) out.push_back(std::move(i));
    }
    return out;
}

// A value as the menu shows it.
inline std::string show(const Item& i, const json& v) {
    if (i.type == "bool") return v.is_boolean() && v.get<bool>() ? "On" : "Off";
    if (i.type == "enum") return v.is_string() ? v.get<std::string>() : "?";
    if (!v.is_number()) return "?";
    const double x = v.get<double>();
    char buf[48];
    const int dec = i.step >= 1 ? 0 : i.step >= 0.1 ? 1 : i.step >= 0.01 ? 2 : 3;
    snprintf(buf, sizeof(buf), "%.*f%s%s", dec, x, i.unit.empty() ? "" : " ", i.unit.c_str());
    return buf;
}

// The value one step on (dir +1 / -1) from v.
inline json stepped(const Item& i, const json& v, int dir) {
    if (i.type == "bool") return !(v.is_boolean() && v.get<bool>());
    if (i.type == "enum") {
        const int n = int(i.values.size());
        int at = 0;
        for (int k = 0; k < n; ++k)
            if (v.is_string() && v.get<std::string>() == i.values[k]) at = k;
        return i.values[((at + dir) % n + n) % n];
    }
    const double x = v.is_number() ? v.get<double>() : i.def.is_number() ? i.def.get<double>() : i.min;
    double y = std::round((x + dir * i.step) / i.step) * i.step;  // onto the step grid
    y = std::min(i.max, std::max(i.min, y));
    return std::round(y * 1e6) / 1e6;
}

enum class Key { Up, Down, Left, Right, Ok, Back };

struct Slot {
    bool used = false;
    std::string saved;             // when, as shown ("27 Oct 23:05")
    json values = json::object();  // the settings it sets (like config.json)
};
constexpr int kSlots = 4;

struct Change { std::string key; json value; };

struct Line {
    std::string text, value;
    bool selected = false, header = false;
};

class Menu {
public:
    explicit Menu(std::vector<Item> items) : items_(std::move(items)) {
        for (const std::string& s : section_order()) add_section(s);
        for (const Item& i : items_)
            if (i.section != "Diagnostics") add_section(i.section);
        add_section("Diagnostics");
    }

    const std::vector<std::string>& sections() const { return sections_; }
    static constexpr int kProfiles = -2;
    int page() const { return page_; }  // -1 = the section list, kProfiles = the profiles page
    void set_slots(std::vector<Slot> slots) {
        slots.resize(kSlots);
        slots_ = std::move(slots);
    }
    const std::vector<Slot>& slots() const { return slots_; }

    // The settings that differ from their defaults (what a profile, like config.json, holds).
    json profile_of(const json& values) const {
        json out = json::object();
        for (const Item& i : items_)
            if (value_of(i, values) != i.def) out[i.key] = value_of(i, values);
        return out;
    }
    int cursor() const { return cursor_; }

    void reset_view() {
        page_ = -1;
        cursor_ = 0;
        confirm_ = false;
    }

    // One key press. `values` = every setting's current value. Returns the settings to change; *close = the menu
    // asks to be closed (Close picked, or B on the section list); *save = a profile slot to save `values` to (-1 none).
    std::vector<Change> press(Key k, const json& values, bool* close, int* save = nullptr) {
        std::vector<Change> out;
        if (close) *close = false;
        if (save) *save = -1;
        const int n = rows();
        if (k != Key::Ok) confirm_ = false;
        switch (k) {
            case Key::Up: cursor_ = (cursor_ + n - 1) % n; break;
            case Key::Down: cursor_ = (cursor_ + 1) % n; break;
            case Key::Back:
                if (page_ == -1) { if (close) *close = true; }
                else { cursor_ = page_ == kProfiles ? int(sections_.size()) : page_; page_ = -1; }
                break;
            case Key::Left:
            case Key::Right:
            case Key::Ok: {
                const int dir = k == Key::Left ? -1 : 1;
                if (page_ == kProfiles) {
                    if (k != Key::Ok) break;
                    if (cursor_ == 2 * kSlots) {  // Back
                        cursor_ = int(sections_.size());
                        page_ = -1;
                        break;
                    }
                    Slot& sl = slots_[cursor_ / 2];
                    const bool load = cursor_ % 2 == 0;
                    if (load && !sl.used) break;
                    if (!confirm_ && (load || sl.used)) { confirm_ = true; break; }  // saving to an empty slot asks once
                    confirm_ = false;
                    if (load) {
                        for (const Item& i : items_) {
                            const json to = sl.values.contains(i.key) ? sl.values[i.key] : i.def;
                            if (value_of(i, values) != to) out.push_back({i.key, to});
                        }
                    } else {
                        sl = {true, "just now", profile_of(values)};
                        if (save) *save = cursor_ / 2;
                    }
                    break;
                }
                if (page_ < 0) {
                    if (k == Key::Ok && cursor_ < int(sections_.size())) { page_ = cursor_; cursor_ = 0; }
                    else if (k == Key::Ok && cursor_ == int(sections_.size())) { page_ = kProfiles; cursor_ = 0; }
                    else if (k == Key::Ok && cursor_ == int(sections_.size()) + 1) {  // reset all
                        if (!confirm_) confirm_ = true;
                        else { for (const Item& i : items_) reset(i, values, out); confirm_ = false; }
                    } else if (k == Key::Ok && close) *close = true;  // Close
                    break;
                }
                const std::vector<int> in = section_items(page_);
                if (cursor_ < int(in.size())) {
                    const Item& i = items_[in[cursor_]];
                    out.push_back({i.key, stepped(i, value_of(i, values), dir)});
                } else if (k == Key::Ok && cursor_ == int(in.size())) {  // reset this section
                    if (!confirm_) confirm_ = true;
                    else { for (int x : in) reset(items_[x], values, out); confirm_ = false; }
                } else if (k == Key::Ok) {  // Back
                    cursor_ = page_;
                    page_ = -1;
                }
                break;
            }
        }
        return out;
    }

    std::string title() const { return page_ == kProfiles ? "Profiles" : page_ < 0 ? "MEC VR settings" : sections_[page_]; }

    // Every row of the page (the panel scrolls them).
    std::vector<Line> lines(const json& values) const {
        std::vector<Line> out;
        if (page_ == kProfiles) {
            for (int s = 0; s < kSlots; ++s) {
                const std::string n = std::to_string(s + 1);
                const bool ask = confirm_ && cursor_ / 2 == s;
                out.push_back({ask && cursor_ % 2 == 0 ? "Load profile " + n + "? Your settings now are replaced. Press A again"
                                                       : "Load profile " + n,
                               slots_[s].used ? slots_[s].saved : "empty"});
                out.push_back({ask && cursor_ % 2 == 1 ? "Replace profile " + n + " with your settings now? Press A again"
                                                       : "Save your settings as profile " + n,
                               ""});
            }
            out.push_back({"Back", ""});
        } else if (page_ < 0) {
            for (size_t s = 0; s < sections_.size(); ++s) {
                char count[24];
                snprintf(count, sizeof(count), "%zu", section_items(int(s)).size());
                out.push_back({sections_[s], count});
            }
            out.push_back({"Profiles", ""});
            out.push_back({confirm_ && cursor_ == int(sections_.size()) + 1 ? "Reset ALL settings? Press A again" : "Reset all to defaults", ""});
            out.push_back({"Close", ""});
        } else {
            const std::vector<int> in = section_items(page_);
            for (int x : in) out.push_back({items_[x].label, show(items_[x], value_of(items_[x], values))});
            out.push_back({confirm_ && cursor_ == int(in.size()) ? "Reset this section? Press A again" : "Reset this section to defaults", ""});
            out.push_back({"Back", ""});
        }
        if (cursor_ < int(out.size())) out[cursor_].selected = true;
        return out;
    }

    // What the selected row does (bottom of the panel).
    std::string help(const json& values) const {
        if (page_ == kProfiles) {
            if (cursor_ == 2 * kSlots) return "A: pick   B: back";
            if (cursor_ % 2 == 0) return slots_[cursor_ / 2].used ? "A: load (asks twice)   B: back" : "Nothing saved here yet   B: back";
            return slots_[cursor_ / 2].used ? "A: save (asks twice: replaces it)   B: back" : "A: save   B: back";
        }
        if (page_ < 0) return cursor_ <= int(sections_.size()) ? "A: open   B: close the menu" : "A: pick";
        const std::vector<int> in = section_items(page_);
        if (cursor_ >= int(in.size())) return "A: pick   B: back";
        const Item& i = items_[in[cursor_]];
        std::string h = "Left/right: change   B: back   (default " + show(i, i.def) + ")";
        if (i.restart) h += "   after a restart";
        if (i.type == "float") {
            char r[64];
            snprintf(r, sizeof(r), "   range %s - %s", show(i, i.min).c_str(), show(i, i.max).c_str());
            h += r;
        }
        (void)values;
        return h;
    }

private:
    void add_section(const std::string& s) {
        if (std::find(sections_.begin(), sections_.end(), s) != sections_.end()) return;
        for (const Item& i : items_)
            if (i.section == s) { sections_.push_back(s); return; }
    }
    std::vector<int> section_items(int s) const {
        std::vector<int> out;
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].section == sections_[s]) out.push_back(int(i));
        return out;
    }
    int rows() const {
        if (page_ == kProfiles) return 2 * kSlots + 1;
        return page_ < 0 ? int(sections_.size()) + 3 : int(section_items(page_).size()) + 2;
    }
    static json value_of(const Item& i, const json& values) { return values.contains(i.key) ? values[i.key] : i.def; }
    static void reset(const Item& i, const json& values, std::vector<Change>& out) {
        if (value_of(i, values) != i.def) out.push_back({i.key, i.def});
    }

    std::vector<Item> items_;
    std::vector<std::string> sections_;
    std::vector<Slot> slots_ = std::vector<Slot>(kSlots);
    int page_ = -1, cursor_ = 0;
    bool confirm_ = false;
};

}  // namespace mecvr::settings_menu

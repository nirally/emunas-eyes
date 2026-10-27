#pragma once
// Stage 9: the in-VR settings menu (core/settings_menu.h for what it holds). Hold the menu button 1 s to open or
// close it; either stick moves (up/down) and changes (left/right), A picks, B goes back. While it is open the game
// gets a neutral pad. Changes go to the main thread (take_changes), which writes them to config.json - applied live
// like any other edit. The panel is drawn with GDI into a texture only when it changes; xr_session shows it as a
// quad layer fixed in the room where you looked when it opened.
#include <string>

#include <nlohmann/json.hpp>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace mecvr::settings_ui {

// Main thread.
void init(const nlohmann::ordered_json& schema);  // read in file order (the menu keeps it)
void set_values(const nlohmann::json& values);  // every setting's current value (after each change)
nlohmann::json take_changes();                  // {key: value, ...}; empty object when none

// Render thread.
bool is_open();
void toggle();
struct Controls {
    float x = 0, y = 0;  // either stick, the larger
    bool ok = false, back = false;
};
// One frame of controller input while open; true when a key fired (for a click buzz).
bool input(const Controls& c, double now_s);
// The panel texture (R8G8B8A8_UNORM, kWidth x kHeight), redrawn if the menu changed; null if it can't be made.
constexpr int kWidth = 1024, kHeight = 1024;
ID3D11Texture2D* texture(ID3D11Device* dev, ID3D11DeviceContext* ctx);
// Desktop tests (diagnostics.menu_flat): Home opens/closes, arrows/Enter/Backspace use it, and the panel is copied
// into the top-left of the flat frame.
void set_flat(bool on);
bool flat();
void on_present_flat(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* back);

}  // namespace mecvr::settings_ui

#pragma once

namespace mecvr::screen_mode {

// Stage 8B (PLAYTEST-8a: Options "really hard to see"; the map's icons "move with the headset so I can't select
// anything"; mission select the same): while the game's own time stands still (pause, options, the map, mission
// select), the game's whole picture is shown as a big flat screen floating in front of you - the camera override
// steps aside (the game's own flat camera and field of view, so its icons line up again), the UI is drawn full
// size, and the headset shows one quad layer, fixed in the room where you looked when it opened.
void set_enabled(bool on);         // ui.menu_screen
// 8E (Q-803: "keep it in 3D but add a setting to make it a big virtual screen"): cutscenes on the screen too. A
// cutscene is the HUD gone for 1 s while the game runs - the health bar is drawn in every gameplay frame, and in
// PLAYTEST-8e..8j the only long gaps were the train cutscene (the cutscene camera can't be told from the gameplay
// one at the camera site). Loading screens count as well.
void set_cutscenes(bool on);       // ui.cutscene_screen
bool cutscenes();
void set_size(float width_m, float distance_m);  // ui.screen_width, ui.screen_distance
void set_flat(bool on);            // diagnostics.screen_flat: also without a headset (desktop tests; no quad layer)

// Render thread, once per Present (from time_sync::game_paused, 10 frames each way so a hitch doesn't flip it).
void on_present();
bool active();                     // any thread
float width();
float distance();

}  // namespace mecvr::screen_mode

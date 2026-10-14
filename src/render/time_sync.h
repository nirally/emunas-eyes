#pragma once

namespace mecvr::time_sync {

// Stage 2 eye sync (D-021). The game renders one eye per frame; without this, everything that
// moves has moved on by the time the right eye is drawn. On each right-eye frame the game timer
// update is skipped and the frame reports dt 0 and no simulation ticks, so the right eye sees the
// same world as the left. The skipped time is not lost: the next frame measures across both.
//
// Finds the frame function's main-timer call by signature and checks the dt/tick getter bytes it
// calls; returns false (eyes stay one frame apart, everything else works) if anything differs.
bool install();

void set_enabled(bool on);  // stereo.sync; Shift+F4 toggles; saved
bool enabled();
bool take_changed();        // toggled by key; main thread saves
void set_hold_dt(double seconds);  // diagnostics.sync_hold_dt: dt held frames report (0 = exactly zero)

// True while the game's own time stands still (pause menu, menus): from the timer the game reads.
bool game_paused();

// Render thread, once per presented frame: Shift+F4, 10-second stats line.
void on_present();

}  // namespace mecvr::time_sync

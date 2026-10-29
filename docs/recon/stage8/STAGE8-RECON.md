# Stage 8 recon - the game's UI (2026-10-26, desktop, 1.0.3.47248, 2880x1620)

## Why the HUD and menus are cut off in the headset
Headset log (PLAYTEST-7m): the game renders 129.5 x 100 deg (16:9); a Quest 2 eye shows L49/R45/U48/D50 deg.
In the frame's tangent space (half-width tan 64.75 = 2.12, half-height tan 50 = 1.19) the left eye sees x in
[-0.54, +0.47], the right eye [-0.47, +0.54], both eyes |x| <= 0.47; vertically about [-1, +0.93].
The health/focus bar sits at x ~ -0.97 (hud_game.png); the pause menu fills x in [-1, -0.55] (menu_pause.png).

## Where the UI is drawn (trace_gameplay.txt, trace_pause.txt)
- Tonemap composite: plain draws (ps F4ACD838, then EF82AEB8) write the back buffer (R8G8B8A8, f28) with depth.
- Compute blur of the back buffer (2880x1620 -> 405x720 -> 720x405): the menus' frosted background.
- Then every UI element: an indexed draw onto the back buffer, vertex buffer of 2.5 MB (the UI's), depth bound.
  Gameplay ~32 per frame (ps F6F15678, F707BBB8, F707DAF8, F77CD578, F77C93B8, F707E478, F6F149F8);
  pause menu ~93 per frame (F77CD6F8 most).
- No other indexed draws hit the back buffer.

## Game options
No HUD-size or safe-zone option seen (Options > Video, Gameplay). Gameplay has toggles: Runner's Vision, enemy
stamina bars, enemy position reticle, stamina and focus, center cursor, subtitles.

## 8A result (desktop, diagnostics.ui_scale_flat)
Scaling each UI draw's viewport/scissor by 0.4 about the centre: the menu becomes a centred panel (ui_pause_04.png),
the health bar comes in to about (-0.39, -0.36) (ui_game_04.png). Menu text ends ~7 px tall at 2880 (~0.3 deg in
the headset; zoom_pause.png) - readability to be judged in the headset.
Open: objective markers are UI draws placed at projected world positions - scaled with the rest, they would sit
nearer the centre than their targets.

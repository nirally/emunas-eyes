# PLAYTEST-2a — First real 3D (~15 min)

**What's new:** each eye now gets its own picture (alternating frames), so the world should have real depth. The picture is still the game's own field of view (a "window" with black around it). Full field of view comes next.

**Before you start, in the game's Options → Video:**
- **Anti-aliasing: off** (or FXAA, not TAA). TAA blends each eye with the other eye's last frame, which would look ghosted.
- **Motion blur: off.**

**Keys:** F4 3D on/off · F5 game camera motion · F6 recentre · F7 head tracking · F3/F2 height · F11 bug report

| # | Do this | Expected | What happened |
|---|---|---|---|
| 1 | Stand still in gameplay. Look at something close (a railing, a wall) and something far (a skyline) | Real depth: near things pop out | |
| 2 | Close one eye, then the other | Each eye sees the world from a slightly different spot | |
| 3 | Press F4 off and on | Off = flat picture, on = depth | |
| 4 | Look at a door or a person | Do they feel the right size? (too small / right / too big) | |
| 5 | Look for shimmering, ghosting, double edges, or anything that looks different in each eye (shadows, reflections, sky, lights) | Note what and where | |
| 6 | Walk slowly, then run | Note any wobble or doubled images when moving (expected while running for now) | |
| 7 | If it happens: a cutscene or a takedown (carried from Stage 1) | Note what you see | |
| 8 | Comfort | nausea / eye strain / right size (1-5) | |

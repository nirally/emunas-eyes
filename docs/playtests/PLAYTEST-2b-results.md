# PLAYTEST-2b results — 2026-10-13

"1. yes it's full 2. pressing F9 made the graphics glitch for a second but other than that I did not see a difference, either way it was a really good experience. 3. 3 sharpness 4. no 5. yes 6. no 7. no nausea no eye strain"
- Full view fills the headset; size right; no edge distortion; no stutter; comfortable.
- Sharpness 3/5 -> render resolution next (DSR + swapchain follows resolution).
- Log: target 100.0° vertical, rendered 129.5 x 100.0. F9 at 23:45:35 went off and on again within 0.6 s (bounce) -> 0.7 s lockout added. The session ended with full_fov saved off (a later F9), so part of the session was windowed.
- Log 23:45:11-13 (a drop: y 67.7 -> 63): FOV steering oscillated (rendered 135° <-> 61°) against the game's own FOV kick -> steering now settles once and holds (D-020a).
- The rest of the log was lost: the next launch overwrote mecvr.log -> previous log now kept as mecvr.prev.log.

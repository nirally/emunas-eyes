# PLAYTEST-1a results — 2026-10-13
1-5: pass. User "loved the experience". Look-around 1:1 at the menu and in game, tilt and lean fine at the menu.
6 (Numpad 0 toggle): FAIL - 60% keyboard; on an external keyboard Numpad0 did nothing (no toggle logged; most likely Num Lock off → Insert). → BUG-002
7-11: not reported.
12 Comfort: no nausea, no eye strain. "I feel really short." → BUG-003
Log analysis: SteamVR/OpenXR 2.17.10, headset "oculus"; roll assertion 0 failures. But in gameplay the rendered roll stayed ~0 while head roll was 1-3°. → BUG-004
Log: docs/playtests/PLAYTEST-1a.log

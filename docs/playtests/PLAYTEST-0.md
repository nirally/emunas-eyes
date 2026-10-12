# PLAYTEST-0 — Mod loads and the game plays normally (no headset)

Time: about 10 minutes. Monitor only. The mod doesn't do anything visible yet except tag the window title.

How to launch: start Mirror's Edge Catalyst from Steam as usual. If nothing happens within a minute, launch again (BUG-001).
How to quit safely: quit from the game's own menu.
If anything goes wrong: create an empty file named `SAFE_MODE` in `...\Mirrors Edge Catalyst\mecvr\` and the mod switches itself off on the next launch.

| # | Do this | Expected | What happened |
|---|---|---|---|
| 1 | Launch the game. Alt+Tab or hover the taskbar icon | Title says `Mirror's Edge™ Catalyst  [MEC VR 0.0.1]` | |
| 2 | Watch the intro and main menu | Same as always, no stutter, no error | |
| 3 | Load your save / continue | Loads as usual | |
| 4 | Play ~5 min: run, wall-run, a fight if one is close | Feels exactly like stock | |
| 5 | Open the in-game photo mode if you know how (optional, NVIDIA overlay Alt+F2) | Works as before, or "not available" (fine) | |
| 6 | Quit from the menu | Game closes cleanly | |

Comfort check: n/a (flat screen).
Afterwards: reply "done" plus anything odd. I'll read the log myself.

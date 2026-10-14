# PLAYTEST-3a results — 2026-10-14

"1 menu button don't work, 2 no pressing the menu button brought the steam menu, 3 no 4 I want it faster 5 no 6 none 7 5"
- Everything else worked with the controllers (move, turn, actions); comfort 5; layout fine as is.
- Menu button: SteamVR keeps the Touch menu button for its dashboard (log: virtual pad OFF each time = session lost focus). -> Pause moved to a tap on the left stick click, map to holding it (TapHold, unit-tested).
- No vibration felt. Log had no vibration values (not logged then). -> per-10 s log of the game's strongest rumble request and haptic call results; virtual pad now claims force-feedback support.
- Turning too slow: right stick is already at full deflection; speed = the game's Controller sensitivity (at default, 1.0). -> raise it in Options > Controls; a mod-side faster smooth turn is a Stage 4 option if the game's maximum isn't enough.
- Cutscene/takedown: none.

# PLAYTEST-2d results — 2026-10-14

"I found out than when eye sync was on looking at faith's body from below was stuttering and flickering. when turning eye sync off it stopped. I also noticed that when locking down with the mouse and letting it go for a few seconds faith camera always go back to the center which is a bit annoying"
- Eye sync: Faith's body flickered when looking down -> BUG-010, root-caused and fixed (D-021a).
- Mouse look-down drifts back to centre after a few seconds: by design of game camera motion (D-014; mouse pitch is removed gradually at 0.35 rad/s so it isn't felt as a snap). The setting camera.pitch_source = game keeps mouse pitch -> asked the user (Q-204).
- Log: simulation +300 ticks per 10 s in every window with sync on (game speed exact); exactly half of game frames held; Shift+F4 used twice (OFF 17:42:43, ON 17:42:47; OFF 17:43:13). One process, log complete (BUG-009 fix works; parent pid and command line logged).
- Not reported: other items of the script (moving things solid, comfort). Not repeated in 2e unless the user volunteers.

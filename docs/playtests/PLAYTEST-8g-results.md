# PLAYTEST-8g results (2026-10-26)

User: "1 yes but pressing F5 fixed it, 2 no, 3 yes, 4 5"

Headset log: PLAYTEST-8g.mecvr.log (session 21:59-22:04, after the 21:5x deploy of D-053).

1. Train shake: still there; F5 (game camera motion off) fixed it. The shake is in the game's camera motion - confirmed.
2. Arms no longer jump (BUG-031 fixed).
3. Rolls, wall-runs, landings: as before (the steadying didn't dull them).
4. Comfort 5

Log: on the train roof (22:02:30-22:03:00) the game camera's per-tick rotation change was 2.85 deg avg; the One Euro filter took off up to 5.7 deg and the shake still showed. F5 off at 22:02:46, on at 22:02:58, off again at 22:04:12 - the saved config now has comfort.game_camera_motion false.

Also in the log: at 22:02:58.216 "stereo OFF (mono)", 46 ms before F5 turned game motion back on - F4 (Fn+4 on the user's 60% keyboard) went in with F5, and the rest of the session ran in mono (BUG-032). Config restored to stereo.mode alternate and comfort.game_camera_motion true (the user's choice before) by the agent.

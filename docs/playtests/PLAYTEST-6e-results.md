# PLAYTEST-6e results — 2026-10-16
First reply ("1 no, 2 no, 3 5") came 2 minutes after the build was installed with no new log - not counted; the user then played (22:44-22:46).
1 Faith's body still came apart sometimes. 2 never walked on her own. 3 comfort 5.
Log (PLAYTEST-6e.log): (a) 22:44:55-22:45:23 full stall push (0.2) with Faith at 0.00 m/s - session start, menu/loading; room-scale shouldn't send a stick there at all. (b) the real fight case: 22:45:41-22:45:59 the player was driving with the stick (held) while also moving physically; with no correction while the stick is used the gap grew to 0.34 m and only closed after letting go.
Fix: room-scale idle while the game is paused/in a menu; while the player drives, the view slides back onto Faith at 0.25 m/s (bleed).

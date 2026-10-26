# PLAYTEST-8f results (2026-10-26)

User: "1 yes but I didnt pressed F5, 2 arms jump back and front but I didnt take screenshot. body is not glitching, 3 4"

Headset log: PLAYTEST-8f.mecvr.log (session 21:41, after the 21:36 deploy). The train part twice (a checkpoint reload at ~21:43:47).

1. Shake on the train: yes, like last time. F5 not tried.
2. The glitch: the arms jump back and front; her body does not glitch. No screenshot.
3. Comfort 4 (down from 5).

Log:
- Shake: the game camera's per-tick rotation change on the train roof (camera at 61-62 m; inside the carriage 51.5 m) 2.2-3.0 deg avg, max 6.5, position change 6-40 mm; inside the carriage 0.1-1.0 deg; desktop running 0.00. Shaking rotation, not position - and comfort.game_camera_motion (on) puts it on the view.
- Arms: hand twist about the forearm 90-138 deg avg on the train (elsewhere 38-66); her shoulders against the view steady (0-6 jumps past 5 cm per 5 s, mostly 0). The forearm takes half the twist; past 180 deg the twist wrapped and the forearm's share flipped side: the arm jumping back and front.

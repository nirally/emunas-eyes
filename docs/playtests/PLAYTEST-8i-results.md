# PLAYTEST-8i results (2026-10-26)

User: "1 same, 2 yes it's like the hands are trying to go inside the headset I dont know why, 3 didnt test, 4 3"

Headset log: PLAYTEST-8i.mecvr.log (session 22:37-22:40, after the 22:3x deploy of D-055).

1. Train: shaking the same.
2. Hands after the cutscene: still glitching - "like the hands are trying to go inside the headset".
3. Not tested.
4. Comfort 3 (down from 5).

Log:
- The detector faded the roof fully (22:39:00-22:39:40: 449 of 449 updates), and the shown view without the head still shook 3.3-3.5 deg per update - more than the game camera's own shake against its heading (2.0-2.4). So the rest is in the parts the fade keeps: the heading and the look pitch. input.mod_turning is off in the user's config (the default), so the view takes the game's raw heading; camera.pitch_source game puts the game's look pitch in it.
- Arms: twist normal again (39-65 deg avg, nothing held); on the roof many arms clamped out of reach (up to 593 of 898). The hands are placed from the shown view: a shaking view moves them against her steady shoulders.

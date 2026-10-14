# PLAYTEST-2d — eye sync: moving things the same in both eyes (~15 min)

Why: the game draws the left eye, then the right eye on the next frame. Until now everything that moves (enemies, Faith's arms, doors, birds, other runners) had moved on a little by the time the right eye was drawn, so moving things could look doubled, shimmery or "off" in depth. The mod now holds the world still while the right eye is drawn. Shift+F4 switches this eye sync off and on so you can compare (F4 alone is still 3D on/off).

Setup: game at 2880x1620 as you left it. Eye sync is on by default.

In the headset:
1. Run a stretch where things move: other people, enemies, traffic/drones, Faith's arms while sprinting. Moving things: solid and in the right depth (1-5)?
2. Press Shift+F4 (eye sync OFF), same kind of stretch. Better, worse or same? Then Shift+F4 again (back ON).
3. With eye sync ON: does the game run at normal speed? (Nothing in slow motion, no speed-ups, jumps land the same as before.)
4. Anything new: stutter, a frozen eye, jerky enemies, sound out of sync?
5. If you happen to see a cutscene or do a takedown, anything wrong there? (optional, carried item)
6. Comfort 1-5: nausea / eye strain.

Reply example: "1: 4, 2: worse off, 3 normal, 4 nothing, 5 none seen, 6 1/1".
The log from this session also checks speed (should show ~300 simulation ticks per 10 s while playing) and the log fix (BUG-009).

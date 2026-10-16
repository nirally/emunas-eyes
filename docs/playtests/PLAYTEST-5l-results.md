# PLAYTEST-5l results — 2026-10-16
"1 yes, 2 yes, 3 fine, 4 yes, 5 no, 6 5": slow reach = normal door open, fast flick = door slam (Heavy attack = the user's right-click slam), no mix-ups, hold-type interaction finished, nothing stray, comfort 5.
Log (PLAYTEST-5l.log): slow touches 0.0-0.9 m/s, flicks 3.0-8.9 m/s (threshold 2.0 sits in the gap). Found: while a hand stayed on something, Interact was re-pressed every ~20 ms (contact flickers frame to frame) - the Gridnode still finished; fixed: a press ends only after 150 ms without contact.

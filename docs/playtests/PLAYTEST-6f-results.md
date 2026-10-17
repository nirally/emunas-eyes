# PLAYTEST-6f results — 2026-10-17
Reply: "1 yes but it's much better now it is occuring less frequently, 2 barely, 3 no, 4 5".
Log (PLAYTEST-6f.log, 11:26-11:29): gap under 0.06 m almost all the time. Exceptions: 11:28:48 (0.49 m, just after "dodge: head moved left") and 11:29:08-12 (0.35 -> 0.18 -> 0.24 m), both with the room-scale stick at 0.87-0.92 (push 0.20 on) and Faith 0.00 m/s; max rumble 32767 at 11:29:07/10 (taking hits). Closed at 11:29:14, ~6 s later.
Fix (BUG-016 part 4): stalled 0.8 s -> view slides onto her at 0.4 m/s.

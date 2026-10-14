# PLAYTEST-4c results — 2026-10-14

"1. yes it's back to normal and comfortable, 2 yes but crouching made my head clip into the ground, so please delay the crouching animation for a short time so my head could be where faith's head should be when crouching,3 3"
- Turning back to the game's stick: normal and comfortable.
- Crouch-slide works (both orders). But the view clipped into the ground: the real crouch (added to the view as head movement) plus the game's slide drop (~0.8 m) = double drop.
- Comfort 3 (the clipping).
- Fix (instead of delaying the animation): while a crouch-started slide runs, the real head's downward offset is blended out over 0.25 s, so the view sits at Faith's sliding head height; it blends back when you stand up. Crouching while standing still keeps the real head offset.

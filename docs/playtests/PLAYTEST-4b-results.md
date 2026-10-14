# PLAYTEST-4b results — 2026-10-14

"1. it's good but when turning with the joystick the body dose not turn with me so I have to move my head manually which is bad, 2 no both fast and slowly, 3 no slides occured, 4 2"
- COMFORT 2 (regression). Log: the game's heading drifted far from the view (-105, +92, -124, -95 deg) despite thousands of injected mouse counts, then caught up in lumps that were taken as "game turns".
- Root cause (agent tests TURNTESTRUN/TURNTESTSLIDE): while the pad moves Faith (running, sliding), Catalyst ignores mouse turning entirely (view -90.7 deg, game -0.1 deg). Standing still (pad idle) it takes the mouse exactly — which is all the earlier simulator tests covered.
- Fix: mod-owned turning off by default (input.mod_turning, experimental); the game turns with the right stick again (Stage 3 behaviour: comfort 5, speed good with the game's Controller sensitivity). Body-follows-head stays off (user preference).
- Crouch: detected twice (DOWN at -0.25 m), the user went down to about 1 m; no slide. Likely the turning chaos (not running straight) and/or crouching before starting to run (held, no fresh press). -> while crouched, starting to run re-presses Down actions so it slides.

# Agent drive loop (no headset needed)

- `send.ps1 key <scancode-hex> [holdMs]` / `send.ps1 mouse <dx> <dy> [steps]` — injects input into the running game (SendInput, scancodes: 39 space, 1C enter, 11 W).
- `topng.ps1 <bmp> <png> [width]` — shrinks a mod screenshot for viewing.
- `bodytest.sh start` — launches the game, skips the intros into the last save, looks down at Faith.
- `bodytest.sh run <label> "key=value,..."` — edits the deployed config.json (hot reload), runs forward 4.5 s and takes two PAIR captures (4 consecutive frames each, neighbouring diffs in mecvr.log).
Mod-side triggers (files in the game's mecvr folder): SHOT (one screenshot), PAIR (4-frame diff sequence), CODEDUMP (+ codedump.txt RVAs).
With diagnostics.zero_ipd = true both eyes share one viewpoint, so a held left->right step should be identical. Turn it off again after testing.

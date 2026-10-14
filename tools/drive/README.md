# Agent drive loop (no headset needed)

Scripts (Git Bash; they call PowerShell with Windows paths):
- `send.ps1 key <scancode-hex> [holdMs]` — injects a key (SendInput). `e0` prefix = extended key (e050 Down, e048 Up). 39 space, 1C enter, 01 esc, 11 W, 2A shift, 43 F9.
  `send.ps1 mouse <dx> <dy> [steps]` — relative mouse move.
- `keyshot.sh <out.png> [keys...]` — presses keys, takes a SHOT, converts it to PNG (for viewing).
- `topng.ps1 <bmp> <png> [width]`, `crop.ps1 <bmp> <png> x y w h [zoom]` — view / zoom mod screenshots.
- `bodytest.sh start [forward]` — launches the game, skips intros and the "servers shut down" / "EA Online" notices, resumes the last save, looks down (or not).
- `bodytest.sh run <label> "key=value,..."` — edits the deployed config.json (hot reload), runs forward 4.5 s, two PAIR captures.
Shift+key: hold 2A in the background (`send.ps1 key 2A 900 &`), then press the key.

Mod triggers (create the file in the game's mecvr folder):
- SHOT one screenshot; PAIR 4 consecutive frames, neighbouring and same-eye diffs + sharpness in mecvr.log.
- TRACE draws/dispatches of two frames (shaders, inputs, outputs) -> trace_<n>.txt.
- VIEWCB the per-view constants of 4 uploads; VELOCITY mean motion vectors at the TAA pass (+ jitter log).
- MEMFIND where the view constants' camera values live in memory; WATCH + watch.txt (hex addresses) -> which code writes them.
- CAPTURE every constant buffer of one frame (needs diagnostics.cb_capture at startup); CODEDUMP + codedump.txt (RVAs).
Developer settings: diagnostics.zero_ipd / ipd_scale / ipd_vertical, taa_motion_fix, motion_fix_debug, motion_fix_jitter.
Always restore the user's config.json afterwards (valid JSON — a parse error silently falls back to defaults).

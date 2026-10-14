# Open Questions

Each question says why it matters and what I'll assume if you don't answer.

### Q-001 — Does your Steam copy currently reach gameplay? — ANSWERED: yes
Why: since EA shut down the servers, some players hang on a loading screen after the intro. Stage 0 needs the game running reliably.
Default if unanswered: assume it works, since you've played it a lot. If it hangs, try blocking the game's internet access in Windows Firewall (nothing to do with the copy protection).

### Q-002 — Build path: from scratch, or hybrid? — ANSWERED: hybrid
Why: there's no existing Catalyst VR mod to build on top of. See D-001.
Default if unanswered: hybrid (my own code, plus MIT-licensed pieces and Frosty data mods).

### Q-201 — (agent recon) Does Frostbite render more than one view per frame if asked?
Why: decides rung 1 vs rung 3 for stereo. Answered by recon, not the user.
Default: try it at the menu first.

### Q-204 — Mouse look up/down: keep it, or head only? — OPEN (asked 2026-10-14)
Context: PLAYTEST-2d, a mouse look-down drifts back to level within a few seconds (game camera motion removes mouse pitch gradually, D-014).
Options: (a) camera.pitch_source = game: mouse pitch is added on top of the head and stays; the view can tilt away from the real horizon. (b) keep auto: the head does up/down, mouse pitch fades out (today). (c) head only, mouse pitch ignored immediately (no drift, no effect).

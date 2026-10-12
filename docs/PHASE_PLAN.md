# Phase Plan — Mirror's Edge Catalyst VR

Mode: agentic
Gate: hard for Stages 0–4, revisit after Stage 4
Target: MirrorsEdgeCatalyst.exe 1.0.3.47248 (Steam build 10351434)

## Status — Mirror's Edge Catalyst VR — 2026-10-13
Mode: agentic | Gate: hard | Prerequisites (before Stage 0)

✅ Built and tested: nothing yet
🔧 Built, not yet tested: nothing yet
🚧 In progress: prerequisites (version pin, save backup, VR check)
⬜ Not started: Stages 0–10
❌ Won't be in Phase 1: teleport movement (breaks momentum parkour, D-003), online features (servers shut down 2023)
❓ Need from you: prerequisite checklist
🐞 Open bugs: none
▶ Next: you work through the prerequisite checklist. Then I start Stage 0 (getting mod code running inside the game).

## Prerequisites
- [x] Toolchain: VS 2026 C++, Windows SDK 10.0.26100, CMake 4.4, git
- [x] Git initialized
- [ ] Steam auto-update set to "only update when I launch it"
- [ ] SteamVR over Steam Link verified with a native VR game
- [ ] Save games backed up
- [x] Game restore path: Steam "Verify integrity of game files". The mod only *adds* files, so uninstall = delete them, then verify
- [ ] EULA/modding policy acknowledged by user
- [x] Other mods: none
- [ ] Python 3 (optional, for helper scripts) — install when needed
- [ ] RenderDoc — install at Stage 2
- [ ] Frosty Mod Manager 1.0.6+ — install at Stage 1

## Stages

Effort: S/M/L. Game-specific criteria are marked ★.

### Stage 0 — Recon and entry (S)
- [ ] Engine and DX11 confirmed from the running process
- [ ] Entry: proxy DLL the game loads by name (rung 3). No official API or managed loader exists. Denuvo is left untouched
- [ ] Log file written from inside the game; log line visible
- [ ] Feature flags, safe mode (flag file → flat launch) and config validation
- [ ] Signature scanning for every hook; exe version check on startup
- [ ] Game restorable to stock (delete files + verify)

### Stage 1 — Camera ownership (M, high risk)
- [ ] Find the camera / RenderView path (frame capture + OpenGameCamera/BF2VR approach)
- [ ] Head pose applied last, unsmoothed, zero roll from game code
- [ ] ★ Head bob, landing dips and camera shake removed (Frosty data + code)
- [ ] ★ Wall-run tilt, rolls, quick-turn 180, bar swings, ziplines, MAG Rope swing: no camera roll or pitch forced on the head
- [ ] ★ Vertical look decoupled from Faith's aim (Camera On Head mod's known limit)
- [ ] ★ Takedowns, deaths/falls, Gridnode sequences, scripted set pieces: fade or world-moves-under-head, never yank
- [ ] Correct world scale

### Stage 2 — Stereo rendering (L, high risk)
- [ ] Highest viable rung (native per-eye → instanced → dual pass). Alternate-eye rendering rejected unless everything else fails
- [ ] ★ Effects from the Helix fix list correct per eye: shadows, lights (incl. compute-shader lights), reflections/SSR, sky/sun/moon, lens flares; motion blur off
- [ ] Culling widened; no eye-locked artifacts; correct size of known objects

### Stage 3 — XR runtime, input, simulator (M)
- [ ] OpenXR via SteamVR; runtime and connection logged on startup
- [ ] Action map with Touch defaults + all controller families in bindings.json
- [ ] Dominant/off hand abstraction; handedness swap end to end
- [ ] Game input injected via action map (gamepad emulation or input hook — decide in brief)
- [ ] Desktop simulator drives the same code paths
- [ ] Haptics pulse(hand, strength, duration)

### Stage 4 — Locomotion and comfort (M)
- [ ] ★ Smooth locomotion (head/controller-relative) mapped to Faith's run with momentum preserved
- [ ] ★ **Smooth turn default** (user hates snap); snap turn available but off
- [ ] Vignette (default: low, user-adjustable), seated/standing, height offset, recenter
- [ ] ★ Physical crouch → crouch/slide; jump binding; roomscale collider
- [ ] ★ Vaults, ledge climbs, wall-runs, wall-climbs, bars, ziplines, ladders/pipes: world moves under fixed head + optional fade per type
- [ ] ★ MAG Rope swing comfort option
- [ ] Teleport: not built (D-003)

### Stage 5 — Physics hands and grabbing (M)
- [ ] Physics-driven hands, collision, break distance, frame-rate independent
- [ ] ★ Collectibles, doors, buttons, Gridnodes by touch; pointer fallback

### Stage 6 — Combat and gadgets (L)
- 6A Holsters: ★ only for gadget equip if needed (no weapons in the game)
- 6B Guns: ★ not applicable (Faith never uses firearms)
- [ ] ★ 6C **Physical punching**: fist velocity + proximity to enemy → light attack aimed at that enemy; min-velocity threshold; haptics on hit; either hand
- [ ] ★ 6C Kicks, heavy and momentum attacks (incl. wall-run/air attacks), Shift dodge: gesture or button, selectable
- [ ] ★ 6D MAG Rope and Disruptor: hand-aimed, trigger-fired

### Stage 7 — Full-body IK (L)
- [ ] Retarget Faith's skeleton; head and hands from tracking; legs from game animation
- [ ] ★ Blend IK arms with parkour animations (climbs, grabs) without breaking them
- [ ] Calibration flow + mirror view; persists

### Stage 8 — HUD, UI, cutscenes, audio (M)
- [ ] UI inventory in manifest; every element typed
- [ ] ★ Runner Vision stays world-anchored; ghost runner line at correct depth
- [ ] ★ HUD is one shader (Helix): find a deeper hook or a render-to-texture split
- [ ] Health/focus on wrist; menus as floating panels; map as curved panel
- [ ] ★ Cinematic cutscenes on virtual screen, head free
- [ ] Controller-matched prompts (text substitution first)
- [ ] Audio listener on head pose

### Stage 9 — In-VR settings menu (M)
- [ ] All schema entries in menu; live apply; profiles; reset; binding + injected pause-menu entry

### Stage 10 — Sweep and package (M)
- [ ] Opening hour end to end, left-handed and seated pass, simulator regression
- [ ] dist/: installer, README, uninstall; no game files redistributed

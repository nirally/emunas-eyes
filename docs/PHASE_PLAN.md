# Phase Plan — Mirror's Edge Catalyst VR

Mode: agentic
Gate: hard for Stages 0–4, revisit after Stage 4
Target: MirrorsEdgeCatalyst.exe 1.0.3.47248 (Steam build 10351434)

## Status — Mirror's Edge Catalyst VR — 2026-10-13
Mode: agentic | Gate: hard | Stage 1 of 10 — headset check 2

✅ Built and tested: Stage 0; head tracking at the menu and in game (look-around 1:1, lean) — PLAYTEST-1a
🔧 Built, not yet tested: head tilt in gameplay (BUG-004), F-key controls (BUG-002), eye-height adjust (BUG-003) → PLAYTEST-1b waiting on you
🚧 In progress: rest of Stage 1 — vertical head bob, cutscenes, takedowns, death camera (guided by PLAYTEST-1b)
⬜ Not started: 3D (Stage 2), controllers, movement, hands, punching, body, HUD, settings menu, packaging
❌ Won't be in Phase 1: teleport movement (D-003), online features (servers shut down)
❓ Need from you: PLAYTEST-1b (~15 min, headset)
🐞 Open bugs: BUG-002/003/004 fixed pending test; BUG-001 sev-4
▶ Next: You: PLAYTEST-1b. Me: head bob + scripted cameras once I see your notes.

## Prerequisites
- [x] Toolchain: VS 2026 C++, Windows SDK 10.0.26100, CMake 4.4, git
- [x] Git initialized
- [x] Steam auto-update set to "only update when I launch it" (user, 2026-10-13)
- [x] SteamVR over Steam Link verified with a native VR game (user)
- [x] Save games backup: declined by user 2026-10-13 (risk accepted; re-offer before Stage 1 camera hooks)
- [x] Game restore path: Steam "Verify integrity of game files". The mod only *adds* files, so uninstall = delete them, then verify
- [x] EULA/modding policy acknowledged by user (2026-10-13)
- [x] Other mods: none
- [ ] Python 3 (optional, for helper scripts) — install when needed
- [ ] RenderDoc — install at Stage 2
- [ ] Frosty Mod Manager 1.0.6+ — install at Stage 1

## Stages

Effort: S/M/L. Game-specific criteria are marked ★.

### Stage 0 — Recon and entry (S)

#### Stage 0 brief
Goal: our code runs inside the game, logs, can be switched off, and leaves the game restorable.
Findings so far: exe is packed (Denuvo + EA Activation64); static imports are hidden. At runtime the game loads WINMM (#38), VERSION (#39), DINPUT8 (#66), NvCameraSDK64 (#70), d3d11 (#74), dxgi (#75) — none of them KnownDLLs. DX11 confirmed; no d3d12 loaded.
Unknowns: does the EA/Denuvo loader honour the app-folder search order for version.dll, or load it from System32 explicitly?
Hypotheses: H1 a `version.dll` proxy in the game folder is loaded before d3d11 (rung 3). H2 fallback: same binary renamed to `dinput8.dll`.
Plan: one proxy DLL (`mecvr`) exporting both version.dll and dinput8.dll sets, forwarding to System32 → logger → exe version check → safe mode → config load/validate/hot-reload → change window title as visible proof → hook IDXGISwapChain::Present (MinHook) and log frame rate.
Verify: log file shows load + Present hook frame count; game window title changes; deleting the file restores stock.
Risk: Denuvo integrity checks on loaded modules (unlikely, ReShade-style proxies are common); Steam overlay also hooks Present.
- [x] Engine and DX11 confirmed from the running process
- [x] Entry: NvCameraSDK64.dll proxy (rung 3, D-005). version.dll rejected (System32-only search). Denuvo untouched
- [x] Log file written from inside the game; window title tagged as visible proof
- [x] Feature flags, safe mode (flag file → flat launch, verified) and config validation + hot reload (verified live)
- [x] Signature scanner (unit-tested) + exe version check on startup (mismatch → safe mode)
- [x] Game restorable to stock: undeploy restores NvCameraSDK64.dll byte-identical (hash verified)
- [x] PLAYTEST-0 passed 2026-10-13: game plays normally with the mod loaded (~6 min gameplay, 125–150 fps, no errors)

### Stage 1 — Camera ownership (M, high risk)

#### Stage 1 brief
Goal: the head pose owns the view, 1:1 and unsmoothed, with no forced motion from game code in any state.
Unknowns: where Catalyst builds its view (GameRenderer → RenderView in other Frostbite titles); whether updateCamera (Ansel) runs outside photo mode; which camera effects are data (Frosty) vs code.
Hypotheses:
  H1 Frostbite reflection: the TypeInfo list (class names + field offsets) can be walked from a known name string → gives GameRenderer/RenderView/camera classes without guessing.
  H2 As in SWBF2 (BF2VR, OpenGameCamera), a GameRenderer singleton holds a RenderView with the final view transform + FOV; overriding it before the frame is built moves the view.
  H3 Ansel updateCamera only runs during photo-mode sessions (game paused) → useful for stereo testing, not gameplay.
  H4 Head bob/shake/landing dips live in data (FaithCharacter / camera assets), removable via a Frosty .fbmod (Camera On Head mod shows the camera rig is data).
Plan: TypeInfo dump at menu (no user time) → locate the RenderView path → observation test: rotate the view 10° for one frame and confirm the world moves → head-pose override (from the simulator first, since OpenXR comes in Stage 3) → strip forced camera motion state by state.
Verify: in-game assertion "head pose was last writer to the view, zero roll"; PLAYTEST-1 (monitor + headset) covering wall-run, roll, quick turn, swing, zipline, MAG Rope, takedown, death, cutscene.
Risk: Denuvo-virtualized code around the camera; patching code bytes avoided — prefer data writes and vtable/function hooks found by signature.
Note: Stage 1 head tracking needs OpenXR, which is formally Stage 3. Minimal OpenXR head-pose read is pulled forward into Stage 1 (D-007).
- [x] Find the camera: view CB, RenderView copy, game camera; gameplay writer SetCameraData hooked by signature (D-009, D-010)
- [~] Head pose applied at the camera setter, unsmoothed, zero roll from game code (assertion live; monitor-verified; headset pending PLAYTEST-1a)
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

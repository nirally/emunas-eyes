# Phase Plan — Mirror's Edge Catalyst VR

Mode: agentic
Gate: hard for Stages 0–4, revisit after Stage 4
Target: MirrorsEdgeCatalyst.exe 1.0.3.47248 (Steam build 10351434)

## Status — Mirror's Edge Catalyst VR — 2026-10-14
Mode: agentic | Gate: hard | Stage 4 of 10 — movement and comfort

✅ Built and tested: Stage 0; Stage 1 (head tracking 1:1, no forced motion, feel-the-roll option, death/respawn fine, comfort excellent)
✅ Stage 2 passed (D-025): true 3D, full headset view, sharp at 2880x1620 (DSR), eyes in sync, anti-aliasing per eye, effects correct in both eyes; size/depth 5/5, comfort 5
✅ Log complete again in PLAYTEST-2d (BUG-009 monitoring; the two-process case hasn't recurred)
✅ Stage 3 passed (D-027): Quest 2 controllers play the whole game; pause = tap left stick click, map = hold; vibration; comfort 5
⚠ Mod turning (180°/s) tried in 4a/4b: the game ignores it while Faith runs → off; the game's stick turning is back. "Run where you look" off (user prefers the stick)
✅ Turning with the game's stick, comfortable; real crouch = slide, view at Faith's head (PLAYTEST-4c/4d: "great", comfort 5)
✅ Room-scale: Faith moves with your steps (4f: much better), walls stop the view, fights better (4e); comfort 5
✅ Recentre = hold right stick click 1 s (PLAYTEST-4g)
🚧 In progress: Stage 4 — next: the Stage 4 gate (a longer session)
⬜ Not started: hands, punching, body, HUD, settings menu, packaging
❌ Won't be in Phase 1: teleport movement (D-003), online features (servers shut down)
❓ Need from you: PLAYTEST-4h, Stage 4 sign-off: one full mission (~20-30 min)
🐞 Open bugs: BUG-007 sev-3 (fight framing); BUG-001 sev-4
▶ Next: You: PLAYTEST-4h. Me: Stage 5 brief (hands) while you play.

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
- [x] Head pose applied at the camera setter (+ render-view step), unsmoothed, zero roll from game code unless opted in (D-014); 0 assertion failures
- [x] ★ Head bob: user "noticeable but fine" (kept); rotation shake/dips removed unless game motion on
- [x] ★ Wall-run, rolls, bar swings, MAG Rope: no forced motion by default; opt-in game motion (D-014) smooth (1f)
- [x] ★ Vertical look from the head (view); aim decoupling itself → Stage 6
- [~] ★ Deaths/falls fine (1f). Takedowns, cutscenes, Gridnodes → carried into Stage 2 playtests (D-016)
- [~] Correct world scale → judged with stereo in Stage 2 (D-016)

### Stage 2 — Stereo rendering (L, high risk)

#### Stage 2 brief
Goal: true per-eye rendering at the headset's own FOV and resolution, with every effect correct per eye. Replaces the Stage 1 mono layer (D-011).
Known: one RenderView per frame is built in exe+0x31a40e0 by looping over a list of "views" ([exe+0x2415e50]: begin +8, end +0x10; 0x1120-byte RenderView entries). The RenderView carries the transform, previous transform (TAA/motion vectors), FOV 0.9599 rad, near/far, aspect (D-009). Projection is reverse-Z infinite with TAA jitter. The Helix 3D Vision fix lists the effects that break in stereo: shadows, CS lights, reflections, sky/sun/moon, lens flares; motion blur off. The engine supports Ansel hi-res capture, which means off-centre projection support exists somewhere.
Unknowns: Q-201 does the engine render more than one view per frame if the list has two entries (rung 1/2)? Q-202 where to inject the asymmetric per-eye projection (the RenderView FOV is symmetric)? Q-203 can the frame be rendered twice per Present (rung 3) without breaking game-thread state, TAA history or streaming?
Hypotheses:
  H1 (rung 1) Add a second view to the view list → Frostbite renders two RenderViews per frame natively (split-screen style) with shared simulation.
  H2 (rung 3) Hook the render-frame entry and run it twice, swapping the RenderView between left and right eye transforms.
  H3 (fallback, labelled) Alternate-eye rendering (BF2VR's method) - rejected unless H1 and H2 fail (D-007 note); it halves per-eye framerate and causes temporal artifacts.
Plan: recon the view list and the render-frame call graph at the menu (no user time) → try H1 with a duplicated view → per-eye target capture → asymmetric projection → effects pass (Helix list), TAA per eye → widen culling → resolution/render scale. Performance guardrail: a GTX 1080 Ti over Steam Link; render scale slider early.
Verify: PLAYTEST-2: depth looks right, objects the right size (D-016), no eye-locked effects, cutscene/takedown check (D-016), comfort.
Risk: high. Frostbite's frame graph may assume one view; per-view resources (TAA history, shadow cascades) may need duplication.
- [x] Highest viable rung: native two-view failed twice (D-017) → alternate-eye rendering (labelled fallback), made whole: pairs shown together (D-019), eye sync (D-021/D-021a), per-eye TAA (D-023)
- [x] ★ Effects correct per eye (D-024): each eye is a full render from its own camera; history-based passes (SSR, AO filter, half-res colour) self-consistent; TAA fixed (D-023); no ghosting at 10x eye separation; motion blur off — confirmed in PLAYTEST-2g
- [x] Culling widened (each eye renders the full both-eye FOV, D-020); no eye-locked artifacts; correct size of known objects (PLAYTEST-2a, 2g)

### Stage 3 — XR runtime, input, simulator (M)

#### Stage 3 brief
Goal: play with the Quest 2 Touch controllers instead of keyboard and mouse: every game action on a controller, dominant hand selectable, vibration, remappable bindings for other controllers.
Known: OpenXR via SteamVR already runs (head pose, eyes, swapchains). Catalyst supports an Xbox gamepad natively (full action set incl. combat, MAG Rope, Disruptor, Runner Vision). BF2VR (Frostbite) fed a virtual gamepad via ViGEm.
Unknowns: Q-301 how Catalyst reads the gamepad (XInput version/entry point, or DirectInput / raw input); Q-302 whether it accepts gamepad and mouse at once (look with the head + mouse pitch while moving with the stick); Q-303 which actions need analog (move, look) vs buttons.
Hypotheses:
  H1 Hook the game's XInputGetState (and XInputSetState for haptics) in-process and return a virtual pad built from the OpenXR action state — no driver install, deterministic, testable with the desktop simulator.
  H2 Fallback: ViGEmBus virtual Xbox pad (needs a driver install by the user).
  H3 Later (Stage 6): direct engine input for gestures (punch, MAG Rope aim) on top of the pad.
Plan: recon at the menu (which XInput DLL/entry the game calls, how often, which user index) → OpenXR action set + Touch bindings (bindings.json) → virtual pad via H1 → map: left stick move, right stick turn (smooth, D-003/Stage 4), A jump, B crouch/slide, triggers attack/shift, grips gadget/Runner Vision, menu → pause; dominant hand swap → haptics → simulator drives the same path.
Verify: in-game test at the menu/gameplay without a headset (simulator pad), then PLAYTEST-3a with the Touch controllers: every action reachable, no stuck inputs, menus navigable, comfort.
Risk: the game may mix keyboard/mouse and pad prompts or switch input mode when both are active; anti-cheat not present (offline).
- [x] OpenXR via SteamVR; runtime and connection logged on startup (Stage 1-2)
- [x] Action map with Touch defaults; Index + simple controller suggested; remapping via SteamVR binding UI (D-026, PLAYTEST-3a/3b)
- [x] Dominant/off hand: input.left_handed mirrors all controls (unit-tested; not headset-tested — user is right-handed)
- [x] Game input injected via action map: virtual Xbox pad through an XInput hook (D-026), verified in game
- [x] Desktop simulator drives the same code paths: PADTEST scripted pad + pad_map unit tests (D-027)
- [x] Haptics: the game's rumble played on the hands, scaled to be felt (PLAYTEST-3c)

### Stage 4 — Locomotion and comfort (M)

#### Stage 4 brief
Goal: moving in VR feels natural and comfortable: forward is where you look, turning is the player's (smooth by default, fast enough, snap optional), real crouching and real walking work, traversal moves are comfortable.
Known: the game turns linearly from raw mouse input (-0.001691 rad per count at the user's sensitivity, symmetric, no acceleration; MOUSETEST). The game camera already passes rolls/tilts/dips (D-014) and its own yaw turns (quick turn, wall-runs).
Design (D-028): the mod owns the view heading W (stick turns + the game's own turns); the game's heading is steered after W + head yaw by injected mouse counts each frame, so Faith's body faces where you look. Injection that the game ignores (climbing, menus) is dropped after 0.25 s; turns the injection doesn't explain are the game's and go to W.
Plan: 4a turning + body-follows-head + physical crouch (built) -> 4b room-scale: real walking moves Faith (push the body toward the head with the move stick, BUG-007) -> 4c comfort: vignette (default low/off), recentre on a button, seated mode, height calibration -> 4d traversal: per-move options (fade/vignette) for vaults, climbs, wall-runs, ziplines, MAG Rope swing.
Verify: simulator tests for each piece (TURNTEST, PADTEST, MOUSETEST), then PLAYTEST-4a..4d; Stage 4 gate: comfort over a full mission chapter.
Risk: injected mouse while the game shows a mouse cursor (menus) -> steering only while the game's time runs and the window is in front.
- [x] ★ Smooth locomotion: the game's own stick movement (momentum native); body-relative by user choice (D-028 update)
- [x] ★ **Smooth turn default**: the game's stick turning (mod turning tried, D-028, off: the game ignores mouse while running); snap turn not offered yet
- [x] Recentre on the controllers: hold right stick click 1 s (D-030); height offset F2/F3 (Stage 1); vignette, seated mode, traversal fades not wanted by the user (D-030)
- [x] ★ Physical crouch → slide (PLAYTEST-4d); jump on the left grip (Stage 3); room-scale: Faith moves with your steps, walls stop the view (D-029, PLAYTEST-4e/4f)
- [x] ★ Vaults, ledge climbs, wall-runs, wall-climbs, bars, ziplines, ladders/pipes: the game camera motion passes through (D-014, user wants it); fades not wanted (D-030)
- [x] ★ MAG Rope swing: game camera motion (D-014/D-015a, PLAYTEST-1f: smooth); no extra option wanted (D-030)
- [x] Teleport: not built, by decision (D-003)

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

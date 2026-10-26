# Phase Plan — Mirror's Edge Catalyst VR

Mode: agentic
Gate: hard for Stages 0–4, revisit after Stage 4
Target: MirrorsEdgeCatalyst.exe 1.0.3.47248 (Steam build 10351434)

## Status — Mirror's Edge Catalyst VR — 2026-10-26
Mode: agentic | Gate: hard | Stage 8 of 10 — HUD, UI, cutscenes, audio

✅ Built and tested: Stage 0; Stage 1 (head tracking 1:1, no forced motion, feel-the-roll option, death/respawn fine, comfort excellent)
✅ Stage 2 passed (D-025): true 3D, full headset view, sharp at 2880x1620 (DSR), eyes in sync, anti-aliasing per eye, effects correct in both eyes; size/depth 5/5, comfort 5
✅ Log complete again in PLAYTEST-2d (BUG-009 monitoring; the two-process case hasn't recurred)
✅ Stage 3 passed (D-027): Quest 2 controllers play the whole game; pause = tap left stick click, map = hold; vibration; comfort 5
⚠ Mod turning (180°/s) tried in 4a/4b: the game ignores it while Faith runs → off; the game's stick turning is back. "Run where you look" off (user prefers the stick)
✅ Turning with the game's stick, comfortable; real crouch = slide, view at Faith's head (PLAYTEST-4c/4d: "great", comfort 5)
✅ Room-scale: Faith moves with your steps (4f: much better), walls stop the view, fights better (4e); comfort 5
✅ Recentre = hold right stick click 1 s (PLAYTEST-4g)
✅ Stage 4 passed (D-032): a full session "great in every way, nothing unwanted"; comfort 5
✅ Stage 5 passed (D-038): a mission with hands on, all good, comfort 5
✅ 5d touch to interact: slow reach = open/use, fast flick = door slam, hold-type works, nothing stray, comfort 5 (PLAYTEST-5k/5l)
✅ 5c hands stop at walls, buzz on contact, nothing odd; size 0.9 right; comfort 5 (PLAYTEST-5j)
✅ 5b gloved hands: on your real hands, fingers follow you, look OK, comfort 5 (PLAYTEST-5i); a bit big → 10% smaller (hands.size)
✅ 5a hand markers at the controllers: one steady box per controller, on the handle, pointing right (PLAYTEST-5h, comfort 5) — after BUG-011 (draw hooks) and BUG-013 (each eye used the other eye's camera, D-034)
✅ Physical punching works: reliable, nothing stray, comfort 5 (PLAYTEST-6a)
✅ Hooks = heavy, doors right, Faith stays with you in fights (PLAYTEST-6b)
✅ Shift by a quick lean/step: easy enough, nothing stray (PLAYTEST-6d)
✅ Faith stays under you in fights: never came apart, slides back OK, walls OK, comfort 5 (PLAYTEST-6g, BUG-016 closed)
✅ Punch = punch, hook = kick; MAG Rope by throwing a hand up, no stray kicks (PLAYTEST-6j, BUG-017 closed)
✅ Disruptor by an open-hand push, held while the hand stays open (PLAYTEST-6k: works, ~2 s, nothing stray, comfort 5)
✅ Stage 6 passed (D-042): combat and gadgets by body, buttons kept, comfort 5 throughout
✅ 7B Faith's own arms follow yours: hands at the gloves, turn with yours, elbows OK, comfort 5 (PLAYTEST-7a)
✅ 7C gloves gone, hands stop at walls, body stays with the head, comfort 5 (PLAYTEST-7b)
✅ Left fingers and right palm fixed (PLAYTEST-7d: BUG-019, BUG-020 closed); arms hold the ledge while hanging (7d: "look ok"); comfort 5
✅ No climbing on her own on ledges/pipes; room-scale on the ground unchanged; comfort 5 (PLAYTEST-7e, BUG-021 closed)
✅ Arms take the game's pose on pipes, ladders, wall climbs and ledges, follow yours on the ground; comfort 5 (PLAYTEST-7f, BUG-022 closed)
✅ 7D Faith's fingers follow yours, look right, fist about right, comfort 5 (PLAYTEST-7g retry, D-046); whole-body glitch fixed (BUG-023 closed); her thumb (the game's) a little odd
✅ 7E Faith punches with the arm you punch with (PLAYTEST-7h: yes; true since 7B)
✅ Stage 7 passed (D-047/D-048): Faith's arms, hands and fingers follow yours; body holds in fights; comfort 5 (PLAYTEST-7l/7m)
⬜ Not started: HUD/menus/cutscenes (Stage 8), settings menu (9), packaging (10)
❌ Won't be in Phase 1: teleport movement (D-003), online features (servers shut down)
❓ Need from you: PLAYTEST-8h (the train roof once more)
🐞 Open bugs: BUG-030 sev-2 (train shakes - fixed again, to confirm); BUG-032 sev-3 (F5 also hit F4: stereo off - fixed, to confirm); BUG-018 sev-3 (body sometimes disconnects from the head); BUG-001 sev-4 (not a mod bug)
⚠ 8A HUD and menus pulled in from the edges (D-049, ui.scale 0.4): PLAYTEST-8a - Options "really hard to see", map icons move with the head
⚠ 8B menus on a big flat screen (D-050): PLAYTEST-8b - showed a still image (BUG-029, fixed); hands at rest "much better" (BUG-027 closed); kicks glitch (BUG-028, fixed); comfort 5
✅ 8C menu screen live, Options/map/mission select usable (BUG-029 closed); kicks fine (BUG-028 closed); health bar whole and readable (D-051); comfort 5 (PLAYTEST-8c)
✅ 8D "KSEC Responding" window readable, nothing else in the top right odd, comfort 5 (PLAYTEST-8d, D-052)
⚠ 8E cutscenes in 3D: "great", "really immersive" (PLAYTEST-8e, The Shard); the train shakes (BUG-030) and her body glitches after the cutscene (BUG-031). Recon: the cutscene camera comes through the gameplay camera site (render view = raw camera in every frame), so a detector for the big-screen setting (Q-803) needs another signal - parked behind the two bugs
⚠ PLAYTEST-8f: shake as before (F5 not tried), arms jump back and front (body fine), comfort 4. Log: the game camera shakes on the train roof; the forearm's twist flipped past 180 deg
🔧 D-053: game camera shake smoothed out of game motion (comfort.steady_game_motion); forearm twist kept continuous; desktop-checked (not on the train - not reachable from the desktop save)
⚠ PLAYTEST-8g: arms fixed (BUG-031 closed), rolls etc. unchanged, comfort 5; the train still shook - F5 fixed it; F5 also hit F4 (stereo off, BUG-032)
🔧 D-054: game motion fades out while the camera shakes back and forth (shake detector); F4 gives way to F5; desktop-checked
▶ Next: You: PLAYTEST-8h (~5 min). Me after it: cutscene big-screen setting (Q-803) or audio (8F), as you prefer

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
- [x] Stage 4 gate: PLAYTEST-4h passed 2026-10-15 (D-032)
Risk: injected mouse while the game shows a mouse cursor (menus) -> steering only while the game's time runs and the window is in front.
- [x] ★ Smooth locomotion: the game's own stick movement (momentum native); body-relative by user choice (D-028 update)
- [x] ★ **Smooth turn default**: the game's stick turning (mod turning tried, D-028, off: the game ignores mouse while running); snap turn not offered yet
- [x] Recentre on the controllers: hold right stick click 1 s (D-030); height offset F2/F3 (Stage 1); vignette, seated mode, traversal fades not wanted by the user (D-030)
- [x] ★ Physical crouch → slide (PLAYTEST-4d); jump on the left grip (Stage 3); room-scale: Faith moves with your steps, walls stop the view (D-029, PLAYTEST-4e/4f)
- [x] ★ Vaults, ledge climbs, wall-runs, wall-climbs, bars, ziplines, ladders/pipes: the game camera motion passes through (D-014, user wants it); fades not wanted (D-030)
- [x] ★ MAG Rope swing: game camera motion (D-014/D-015a, PLAYTEST-1f: smooth); no extra option wanted (D-030)
- [x] Teleport: not built, by decision (D-003)

### Stage 5 — Physics hands and grabbing (M)

#### Stage 5 brief
Goal: your real hands are in the game: you see them where your controllers are, they don't pass through walls, their fingers follow how you hold the controllers, and you use the world with them (reach and grab to interact) instead of a button where it makes sense.
Known: OpenXR already runs; controller poses are one action away (grip/aim pose actions). We own the frame: the per-view constants give the view/projection each eye was drawn with (D-023), the scene depth and the HDR colour target before tonemapping are known (TAA recon: depth t0, scene colour r1, composite at the end). Faith's own first-person arms are game animation (climbs, attacks, ledge grabs, Gridnodes). The game's interact is "A when a prompt shows" (Stage 3).
Unknowns:
  Q-501 (user) what the hands look like and what happens to Faith's own arms (see QUESTIONS.md).
  Q-502 where to draw our hands so they're lit/exposed like the scene: after TAA on the HDR target, before bloom + tonemap (expected) — check exposure and fog match.
  Q-503 can Faith's game arms be told apart in the frame (own draws/shaders/vertex buffers) to hide them, or does that wait for Stage 7 IK? — ANSWERED (D-036): yes, stencil 0x20 marks her body.
  Q-504 is the game's interact prompt readable (memory or UI draw), so "touch to interact" only fires when an interaction is actually available? — CLOSED (D-037): not needed, A without a prompt does nothing.
  Q-505 pose timing: predicted controller pose for the frame's display time vs the frame the game simulated (eye sync D-021 pairs).
Hypotheses:
  H1 Our own hand meshes drawn into the HDR scene target with the frame's own view-projection, depth-tested against the scene depth (reverse-Z), simple lighting from the sun direction/ambient in the view constants — no engine changes, independent of game animations.
  H2 Collision without engine physics: the hand is held at the first surface along the line from the shoulder to the controller, from the scene depth (screen-space), plus a short haptic tick on contact; break distance: past 0.3 m from the real controller the hand snaps back.
  H3 Fingers from Touch capacitive/analog inputs (trigger/grip values, thumb touch) -> open / point / fist poses blended.
  H4 Interact by reach: grip near the centre of the view while the game shows its interact prompt -> press Interact (A); A stays as the pointer-free fallback.
Plan (each step tested in game with the drive tools, then a short playtest):
  5a controller poses: OpenXR grip/aim poses -> world (same body frame as the head), logged and shown as simple debug shapes at the controllers, to verify they line up with your real hands in the headset (PLAYTEST-5a).
  5b hand meshes: a glove hand model (CC0/MIT asset or procedural), lit/exposed like the scene, depth-tested; fingers from controller inputs.
  5c collision + haptics: depth-buffer clamp, break distance, contact buzz.
  5d interaction: reach-and-grab Interact while the prompt shows; prompt detection recon (Q-504).
  5e Faith's own arms per Q-501 (hide, or keep until Stage 7 IK replaces them). — DONE by Q-501 (a): kept until Stage 7; hiding them is possible (stencil 0x20, D-036).
Verify: hands line up with real hands (5a), look and light right (5b), don't pass through walls (5c), interacting by reach works on doors/buttons/Gridnodes/collectibles (5d); comfort; Stage 5 gate = a mission played with hands on.
Risk: lighting mismatch (hands look pasted on), hands drawn over the game's own UI, double hands while Faith's arms animate (Q-501/Q-503), cost of an extra pass (small).
- [x] 5a Hands at the controllers (D-031, D-034): markers line up with the real controllers (PLAYTEST-5h)
- [x] 5b gloved hands with fingers (D-035): fit, fingers and look OK (PLAYTEST-5i); size 0.9 (hands.size)
- [x] 5c hands stop at walls + contact buzz (D-036): PLAYTEST-5j
- [x] 5d touch to interact (D-037): doors, buttons, Gridnode hold by touch; flick = slam (PLAYTEST-5l)
- [x] ★ Doors, buttons, Gridnodes by touch; A stays as fallback; collectibles: check in the gate session

### Stage 6 — Combat and gadgets (L)

#### Stage 6 brief
Goal: fight with your body: punches thrown with your fists become Faith's attacks; the other combat moves and the gadgets (MAG Rope, Disruptor) work by hand where it makes sense, with the buttons always kept as a fallback.
Known: the game's pad layout (D-027): X Light attack, Y Heavy attack (with the move stick = directional heavy), RT Shift, B Disruptor, LB/LT up/down actions. Catalyst picks the attack target itself (the enemy ahead), so the mod only has to decide *when* and *which* attack. Hand speed relative to the head is already tracked (5d); touching something fast already = Heavy (door slam, D-037).
Unknowns:
  Q-601 (user) which swing should be which attack — ANSWERED (PLAYTEST-6a): straight = light, hook = heavy
  Q-602 does a Light attack also slam doors open (then punch vs flick needn't be told apart at doors)?
  Q-603 can enemies be found (position) for "punch only near an enemy"? Only if accidental punches are a problem.
Hypotheses:
  H1 punch_model: hand velocity relative to the head > 2.5 m/s, forward within ~50 deg of the view (or across the front = hook), hand in front of the body, one punch per swing (re-arm below 1 m/s, 0.25 s apart) -> Light attack. Reaching (< 1 m/s) never fires.
  H2 a punch that hits a surface within 100 ms was a door flick: the slam goes out instead, never both.
Plan: 6C-1 punching (PLAYTEST-6a) -> 6C-2 heavy/kick, Shift, wall-run/air attacks per Q-601/Q-602 answers -> 6D MAG Rope + Disruptor aimed by hand.
Verify: punches land on enemies in a fight, no punches while reaching, climbing or gesturing; comfort.
Risk: accidental attacks while waving hands in parkour; latency of the 100 ms door check (feel).
- 6A Holsters: ★ only for gadget equip if needed (no weapons in the game)
- 6B Guns: ★ not applicable (Faith never uses firearms)
- [x] ★ 6C **Physical punching**: straight = light, hook = heavy, either hand, buzz on the punch (D-039, PLAYTEST-6a/6b)
- [x] ★ 6C heavy = hook (PLAYTEST-6b); wall-run/air attacks come from the game when you punch while wall-running/jumping
- [x] ★ 6C Shift dodge by a quick lean/step sideways or back, trigger kept (D-040, PLAYTEST-6d)
- [x] ★ 6D MAG Rope (throw up past the head, lower to let go) and Disruptor (open-hand push, held while open), game aims by the view (Q-604, D-041, PLAYTEST-6j/6k)

### Stage 7 — Full-body IK (L)

#### Stage 7 brief
Goal: Faith's own arms and hands follow your real arms and hands (two-bone IK), legs and body stay the game's; the game's arm animation is kept where it matters (climbs, grabs).
Known (docs/recon/STAGE7-RECON.md, confirmed by trace + poke): Faith's body is skinned in the vertex shader from per-frame world-space 3x4 bone matrices the CPU writes into a 1 MB dynamic buffer; each body piece's (start, count) is in its per-draw constants. Both arms + hands are one 81-bone piece. Rewriting its rows (Map NO_OVERWRITE) right before its draw re-poses it on screen. The arm chains (shoulder -> elbow 0.282 m -> wrist ~0.24 m -> fingers) and joint points come out of the matrices themselves (tools/bone_solve, 600 frames).
Unknowns:
  Q-701 (user, at the first IK playtest) during climbs, ledge grabs and other parkour moves: should Faith's arms show the game's animation (her hands on the ledge) or always follow yours?
  Q-702 (user) once Faith's arms follow yours, hide the mod's gloved hands (5b)? Default: hide; option to show.
  Which solved arm is Faith's left (her facing from the per-draw world matrix); stable identification of the arm piece and its bones across sessions, LODs and outfits.
Hypotheses:
  H1 IK by correction: S' = C_seg * S for every bone on a segment (upper arm, forearm, hand+fingers), C from a two-bone solve with the game's shoulder fixed, elbow pole down-and-out, hand = controller grip pose (+ calibrated offset), reach clamped.
  H2 joints and segment membership solved at runtime from a few seconds of the game's own motion (bone_solve logic in core, unit-tested), not hard-coded indices.
Plan: 7A arm piece + joints at runtime (log, simulator-checked) -> 7B IK arms with simulated hands (desktop screenshots) -> PLAYTEST-7a (arms follow; Q-701/Q-702) -> 7C hide the gloves (Q-702) -> (blend with parkour animations dropped: Q-701 = always yours) -> 7D fingers from finger touch -> 7E punches by the arm you punch with -> calibration (arm length, grip offset).
Verify: looking down and in a mirror-like view (desktop third-person debug or reflections), Faith's hands sit on the controllers, elbows bend naturally, no inversion or popping; comfort.
Risk: the world matrices are rendered positions - the game's own logic (hit boxes, ledge hands) doesn't know about the IK pose (visual only, intended). Index changes between meshes/LODs. Two eyes per frame pair: the correction must be the same for both eyes' draws.
- [x] Recon: Faith's arm bones found, re-posable before the draw, skeleton solved from the matrices (STAGE7-RECON.md)
- [x] Head and hands from tracking, legs and body from the game's animation (full retarget not needed: brief goal met)
- [x] ★ Faith punches with the arm you punch with (PLAYTEST-6a request): IK arms carry the attack instead of the game's combo choice (PLAYTEST-7h)
- [x] ★ Blend IK arms with parkour animations (climbs, grabs) without breaking them: the game's arms while a game hand is above the shoulder, yours otherwise, 18-frame fades (PLAYTEST-7f)
- [x] Calibration: reach scaling built (D-048, body.reach_scale) but off by default - PLAYTEST-7m liked the unscaled arms more; no flow needed

### Stage 8 — HUD, UI, cutscenes, audio (M)
#### Stage 8 brief
Goal: every UI element at a comfortable depth with the treatment for its type; menus and the map usable in the headset; cutscenes watchable without forced camera motion; sound turning with your head.
Known: today the HUD, menus and cutscenes go through as the game draws them, into each eye's frame (alternate-frame stereo). Helix (3D Vision fix) found the HUD is one shader/texture that can't be split by element. No playtest so far has complained about the HUD, menus or cutscenes (cutscenes/takedowns "nothing wrong", PLAYTEST-4h).
Unknowns: Q-801-Q-804 (what bothers the user now); where the HUD pass is in the frame (one draw? a separate render target?); at what depth it shows now (same screen spot in both eyes = at infinity); whether menus and the map are the same pass; how cutscene cameras reach the view (Stage 1 owns gameplay cameras); whether the audio listener follows the game camera or our head pose.
Hypotheses: H1 the HUD is drawn last into the back buffer with its own shader (Helix) - findable by shader in our draw hooks and redirectable to an offscreen texture shown as a panel at a set depth. H2 Runner Vision and objective markers are world-space draws already (right in stereo); only the flat HUD layer needs moving. H3 the listener is set from the camera the game thinks it has (our head is applied at render time), so sound may not turn with the head.
Plan: Q-801-Q-804 -> desktop recon (HUD draw and its depth now, menus/map, cutscene camera path, listener) -> 8A HUD panel at depth -> 8B menus/map panel -> 8C health bar to the bottom middle (PLAYTEST-8b) -> 8D top-right alerts in (PLAYTEST-8c) -> 8E cutscenes (per Q-803) -> 8F audio listener -> 8G prompts showing the Touch buttons (text substitution).
Verify: PLAYTEST-8x per sub-stage; comfort.
Risk: one HUD shader = all-or-nothing, no per-element anchoring (wrist health) without finding each element's draws; menus may share the HUD pass.
- [ ] UI inventory in manifest; every element typed
- [ ] ★ Runner Vision stays world-anchored; ghost runner line at correct depth
- [x] ★ HUD is one shader (Helix): each element is its own indexed draw, placeable by its vertices (D-049, D-051)
- [ ] Health/focus on wrist; menus as floating panels; map as curved panel
- [ ] ★ Cinematic cutscenes on virtual screen, head free
- [ ] Controller-matched prompts (text substitution first)
- [ ] Audio listener on head pose

### Stage 9 — In-VR settings menu (M)
- [ ] All schema entries in menu; live apply; profiles; reset; binding + injected pause-menu entry

### Stage 10 — Sweep and package (M)
- [ ] Opening hour end to end, left-handed and seated pass, simulator regression
- [ ] dist/: installer, README, uninstall; no game files redistributed


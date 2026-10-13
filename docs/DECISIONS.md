# Decisions (append-only)

### D-000 — Target the Steam copy only
Date: 2026-10-13  Stage: setup
Chose: build and test against `E:\SteamLibrary\...\Mirrors Edge Catalyst` (exe 1.0.3.47248).
Rejected: the DRM-removed copy in `E:\Games\`. We don't work on circumvented builds, and other users will have the Steam version.
Rule: never modify, bypass or work around Denuvo, the license token or the EA login.

### D-001 — Prior work survey (choice pending, see Q-002)
Date: 2026-10-13  Stage: setup

| Candidate | Provides | License | Status | Usable how |
|---|---|---|---|---|
| BF2VR (Battlefront II) | Frostbite camera via RenderView, alternate-eye stereo, OpenXR, ViGEm input | GPL-3.0 | Archived Nov 2023, different game | Study approach. Code reuse would force our mod to be GPL-3 |
| OpenGameCamera | Frostbite SDK classes, DebugRenderer hooks, MinHook/vtable helpers | MIT | Active-ish, targets SWBF2 | Vendor small helpers with attribution |
| Frosty Tool Suite / Mod Manager | Data edits (camera rig, character, UI assets) | Open source (tool only) | Maintained, supports Catalyst | Use as a tool. Ship `.fbmod` files for data changes |
| Camera On Head (Nexus #332) | Shows which camera data fields exist | Unstated | 2025 | Study only (approach, not files) |
| Helix 3D Vision fix | Map of shaders broken in stereo | Unstated | 2018 | Study only |
| VorpX | Fake 3D | Commercial | — | Not applicable |

No maintained VR framework exists for Frostbite, and no VR mod exists for Catalyst, so "build on top" isn't available.
Recommendation: **hybrid**. Our own native DLL (D3D11 stereo, OpenXR via SteamVR, camera, input), vendoring only MIT-licensed helpers, plus Frosty `.fbmod` data edits for camera-rig and HUD tweaks.

### D-002 — Build path: hybrid
Date: 2026-10-13  Stage: setup
Chose: hybrid. Our own native DLL for stereo, camera, OpenXR and input; vendor only MIT/permissive helpers with attribution; use Frosty `.fbmod` data edits for camera-rig and HUD data.
Rejected: build-on-top (no framework exists); pure from-scratch (no benefit over hybrid).
Note: no GPL-3 code from BF2VR is vendored unless we deliberately choose to license the mod GPL-3.

### D-003 — No teleport locomotion
Date: 2026-10-13  Stage: planning
Chose: smooth locomotion only. Teleport is listed as a known gap.
Reason: Catalyst's movement is momentum-based parkour (wall-runs, vaults and jumps depend on speed). Teleport would bypass the game's movement model and break traversal. The user prefers smooth locomotion anyway.
Cost: users who need teleport for comfort get the vignette and per-move fades instead.

### D-004 — Comfort defaults adjusted to the user
Date: 2026-10-13  Stage: planning
Chose: smooth turn on by default, snap turn available but off; vignette on at low strength (0.3).
Rejected: the standard comfort-first default (snap turn, medium vignette). The user says they love smooth locomotion and hate snap.
Revisit: at packaging (Stage 10), reconsider shipping defaults for new VR users. The user's own profile keeps these.

### D-005 — Entry: NvCameraSDK64.dll proxy (rung 3)
Date: 2026-10-13  Stage: 0
Chose: replace the game's NvCameraSDK64.dll (NVIDIA Ansel SDK library, in the game folder) with our proxy. The original moves to `mecvr\NvCameraSDK64_orig.dll`, and all 9 exports forward through counting stubs.
Rejected: version.dll proxy (H1). Tested: the game loaded System32\version.dll and ignored ours; the launcher layer restricts system-DLL search to System32, so dinput8/winmm/dxgi proxies would fail the same way. General injector (rung 4): not needed.
Bonus: the Ansel interface (updateCamera / setUpdateCameraFunc) is a candidate camera hook for Stage 1.
Cost: we replace one game file. The installer keeps the original; undeploy restores it byte-identical (verified by hash). Steam Verify also restores it.
DRM: untouched. We don't modify the exe, Activation64 or Denuvo.

### D-006 — Save backup declined
Date: 2026-10-13  Stage: prerequisites
The user chose to skip the save backup. Risk accepted. Re-offer before Stage 1 writes to camera/player state.

### D-007 — Minimal OpenXR head pose pulled into Stage 1
Date: 2026-10-13  Stage: 1
Chose: Stage 1 reads the headset pose through a minimal OpenXR session (head only, no rendering to the headset yet), so camera ownership can be tested with real head movement. The full runtime/input layer stays in Stage 3.
Reason: "1:1 with the headset" can't be verified without the headset's pose. The desktop simulator (mouse-look head) covers the no-headset checks.

### D-008 — Stage 1 H1 rejected: no reflection names in the exe
Date: 2026-10-13  Stage: 1
Evidence: a whole-process string probe at the main menu. The control string "d3d11.dll" was found (scanner works). "GameRenderer", "ClientGameContext" and "FaithCharacter" are absent from the exe image ("GameRenderer" only appears 5x in heap data). "CameraComponent" appears 17x and "RenderView" once in the exe, as plain strings, not as a TypeInfo list.
Conclusion: Catalyst's retail build strips TypeInfo names, so a name-driven SDK walk isn't available.
Next rung: "How to find" #3, frame capture. Hook constant-buffer uploads, capture one frame, identify the view/projection matrices by shape, then value-hunt the CPU-side camera struct and the code that writes it.
Also noted: the typeinfo scanner merged regions, so one faulting page hid the whole image. Fixed in recon_probe (per-region guard).

### D-009 — Camera located (main menu): view CB, RenderView copy, game camera + stable chain
Date: 2026-10-13  Stage: 1
Method: "How to find" #3 + #4 + #5. Constant-buffer capture → value hunt → poke test → pointer scan.
- View constant buffer: 1136 bytes. [4..7] screen size, [8..23] view (row-major storage, translation in column 3, camera = -R^T t), [24..39] projection (reverse-Z infinite, near 0.06, TAA jitter in [2],[6]).
- 2,153–2,440 copies of the camera position in memory; ~200 full LinearTransforms (right, up, forward, trans; 64 bytes). Exactly 2 move the rendered view when written (+3.000 m, 3 runs).
- Upstream "game camera": writing it propagates to the render copy. Reached by **[[exe+0x23e7f78]+0x400]+0x70** (position; rotation rows at +0x40/+0x50/+0x60). Verified on 3 launches.
- Downstream "RenderView" copy: transform, previous transform at -0x40, FOV 0.9599 rad (55° vertical) at +0x74/+0x78, near 0.05 at +0x7c, far 10000 at +0x80, aspect 1.7778 at +0x84 (offsets from the position). No short static path.
- Menu camera never moves, so no writers caught yet (hardware write-watch saw 0 writes). The writers and the gameplay camera need a gameplay session (RECON-1).
Rejected: blind poking of all candidates (crashed the game at the menu once; no save involved). Now filtered to live full transforms.

### D-010 — Stage 1 hook point: gameplay camera setter, filtered by call site
Date: 2026-10-13  Stage: 1
Evidence (RECON-1 + code dump):
- The menu chain [[exe+0x23e7f78]+0x400]+0x70 does NOT hold in gameplay (resolves to zeros). It's menu-only.
- Gameplay hunt (paused): 2 feeders again. The upstream one is written once per frame while moving, by exactly one site.
- Writer = exe+0x3536f10, `SetCameraData(cam=rcx, const data=rdx)`: compares data[0x00..0x3f] (transform A) and data[0x80] (xyz) with the camera, and if different copies 0x90 bytes (A at +0x00, transform B at +0x40, vec at +0x80) and sets dirty flags |= 0xb at cam+0x120.
- Caller = gameplay camera manager (chunk exe+0x327ba58..0x327bcad): asks the active camera mode (rdi, vtable) for this frame's data, copies rdi+0x10..0x9f into mgr+0x90, applies other setters (0x3536db0, 0x3536a70, 0x3536fc0, 0x3532dc0), copies mgr+0x90 → mgr+0x120, then calls 0x3536f10(renderCam=rsi, mgr+0x120); return site exe+0x327bc8b. Then 0x35369e0(renderCam, [mgr+0x20]) (probably FOV).
- Pause/menu writers are generic struct copies (0x352ca40, 0x352d810), not usable as hooks.
Chose: hook 0x3536f10 (found by signature scan, not offset). When the return address is the gameplay-manager site, replace transforms A and B in the data with: game yaw + game position + head pose. The game's own copy (mgr+0x90) is untouched, so game logic doesn't feed back on our change. Other call sites are logged (cutscene/menu cameras: later in Stage 1).
Risk: Denuvo integrity checks on patched code. MinHook writes a 5-byte jump at the function start (0x3536f10 is a plain leaf, not virtualized). If the game reacts badly, fall back to hooking the vtable slot of the camera mode (data-only patch).

### D-011 — Stage 1 headset path: mono world-locked layer at the game's FOV
Date: 2026-10-13  Stage: 1
Chose: OpenXR (loader 1.1.63, loaded from mecvr\ by full path) through SteamVR. The game's back buffer is copied into an sRGB swapchain and submitted as one projection layer for both eyes, using the game's own symmetric FOV (from the view constants) and the exact head pose the frame was rendered with (matched by rendered rotation). The runtime places it world-locked: 1:1 look-around without stereo.
Labelled to the user as "flat picture, not 3D yet". Stereo replaces this in Stage 2.
Pitch: from the head when a headset session runs (camera.pitch_source=auto); mouse pitch is ignored for the view (aim decoupling is Stage 6).
Failure path verified: no runtime/headset → logs the reason, stays flat, simulator works.
Known open in Stage 1: positional head bob from the game camera (rotation bob/roll already removed), cutscenes, takedowns, death camera.

### D-012 — Second override point: the gameplay render-view build
Date: 2026-10-13  Stage: 1
Evidence: PLAYTEST-1a log (roll lost only in gameplay) + disassembly of exe+0x31a40e0: per view, 0x3277100 fills the RenderView from the camera, then `if (0x319c0e0(...)) SetCameraData(renderView, derived)`. The call returns to exe+0x31a43be and runs once per frame, only in gameplay.
Chose: the same SetCameraData hook, filtered on this second return address (signature-scanned). When the derived forward is within 15° of our final view, replace rotation and position with our final ones. Otherwise pass through, counted and logged every 10 s.
Open question: what 0x319c0e0 represents (it keeps yaw/pitch and drops roll). The pass-through count will show whether it ever produces a different camera (scripted moments).

### D-013 — Render-view derived camera comes from the game camera; head smoothing declined
Date: 2026-10-13  Stage: 1
Evidence: PLAYTEST-1b counters (BUG-005). The derived camera at exe+0x31a43be follows the game camera (position within 0.03–0.2 m of ours, which equals our head offset + height offset; direction off by the head's yaw/pitch).
Chose: replace it with our final view when it is within 10° of the game camera or 15° of ours; pass others.
Declined: smoothing the head pose on request ("game snapping to my position too firmly"). Reason: the symptom matches BUG-005 exactly, and smoothing the head pose adds latency between head and view, the main cause of VR sickness (operating principle: never smoothed). Will offer an opt-in only if the user still wants it after the fix, and never for rotation.
Calibration data: user eye height 1.48–1.55 m (STAGE), prefers +0.14 m camera height.

### D-014 — Opt-in "game camera motion" (rolls, tilts, dips felt in VR)
Date: 2026-10-13  Stage: 1
User request (PLAYTEST-1d): "I really want to feel the roll with the camera. I don't care about nausea or eye strain."
Chose: setting comfort.game_camera_motion (F5 toggles live, saved). When on, the game camera's rotation relative to its heading (minus the tracked look-pitch baseline) is applied between the body heading and the head pose: final = yaw(h) * pitch(-baseline) * yaw(-h) * game * head. The look pitch (mouse) stays excluded; the baseline is tracked only while the camera is calm (roll < 2°, upright, pitch rate < 0.5 rad/s). The roll assertion is suspended while on.
Defaults: OFF in schema (comfort-first for anyone else); ON in this user's config.
Also: heading_of made exact for yaw*pitch*roll cameras at any pitch (sign of up.y), right-axis fallback at vertical. Unit-checked (full tumble, tilt+pitch, motion formula).
Risk accepted by the user: forced camera rotation is a known nausea trigger. The F5 toggle is the escape hatch.

### D-015 — Compose from the game's blended camera in gameplay
Date: 2026-10-13  Stage: 1
Chose: in gameplay, build the VR view at the render-view step from the derived (blended) camera, the one a flat player sees, rather than overwriting it with a view built from the raw camera manager output. The main site still composes from raw (it's the only camera at the menu, and keeps the camera object consistent). Each site tracks its own look-pitch baseline; the baseline moves at most 0.35 rad/s (never a step).
Reason: PLAYTEST-1e snappiness on movement-mode transitions (roll entry, pole, MAG Rope) - BUG-008.

### D-015a — Correction to D-015
Date: 2026-10-13  Stage: 1
PLAYTEST-1f showed the render-view camera is identical to the raw camera (max blend 0.0°). The premise of D-015 ("the game blends transitions there") was false. The snappiness fix came from the gradual look-pitch baseline. Composing at the render-view step from its own input is kept: it's equivalent today and remains correct if a scripted camera ever arrives there.

### D-016 — Stage 1 gate: pass, with two items carried into Stage 2 playtests
Date: 2026-10-13  Stage: 1
Passed on: 1:1 head tracking accepted by the user (PLAYTEST-1c), no game roll unless opted in (assertion 0 failures across sessions), roll and swing transitions smooth (1f), wall-run, death/respawn reported fine (1f), comfort excellent (no nausea or eye strain in 6 sessions).
Carried: (1) cutscenes and takedowns. The user couldn't trigger them on demand; they get a fixed item in every Stage 2 playtest until seen. (2) World scale is judged properly only with stereo, so it's part of the Stage 2 exit ("objects feel the right size").
Not applicable: vehicles (none in the game).
Moved to later stages by design: vertical look decoupled from aim (Stage 6), body following real movement / camera ahead of Faith (Stage 4, BUG-007), head bob (user: "noticeable but fine").

### D-017 — Stage 2 H1 (native two-view via the split-screen builder): two attempts failed; moving to H2
Date: 2026-10-13  Stage: 2
Attempt A: a byte copy of the view object added to the view list for the builder call → crash at exe+0x2c7fa3f, a doubly-linked-list unlink (node->prev->next = node->next). The view object contains intrusive list links that a copy shares with the original.
Attempt B: the same view object listed twice, with the viewport switched per FillRenderView (exe+0x3277100, hooked via its call site) → the render thread stopped presenting immediately (hang); crash at exe+0x3b729e1 on close.
Conclusion: a second view has to be created by the engine's own view factory (unknown; would need its constructor and arguments). Parked as a later revisit, not abandoned.
Next rung (H2, planned in the Stage 2 brief): sequential per-eye rendering, alternate eyes per frame, per-eye swapchains, each eye submitted with the pose it was rendered at. Step 1: plain alternation (eyes ~1 frame apart). Step 2: synchronise by freezing game time on the second eye's frame.
Both experiments stay behind diagnostics.stereo_experiment (default off).

### D-018 — H2 step 1: alternate-eye rendering implemented
Date: 2026-10-13  Stage: 2
Each game frame renders one eye: the main camera site toggles the eye once per frame; both override sites offset the camera by that eye's position and rotation in head space (from xrLocateViews; logged IPD and FOV). Rendered frames are matched back to their eye by rotation + position (eyes sit an IPD apart). Two OpenXR swapchains; each frame's image goes to its eye's swapchain, and both eyes are submitted each frame at the poses their latest images were rendered with.
Known limits (labelled to the user): eyes are one frame apart (moving scenes, especially running, show stereo error until step 2 synchronises them); TAA and motion blur mix eyes, so the user disables them for now; FOV is still the game's.
Toggles: stereo.mode (F4), saved.

### D-019 — Stereo pairs: both eyes share one head pose and game camera, shown together
Date: 2026-10-13  Stage: 2
PLAYTEST-2a (partial): with D-018 the head tracking "was vibrating / a bit flickering"; the user pressed F4, which turned 3D *off* (log: "stereo OFF (mono)"), and the rest of the session was flat. Cause: each eye was drawn with its own head pose and game camera one frame apart and shown as soon as it existed, so the two eyes disagreed by one frame of head and camera motion, changing every frame.
Fix: the right-eye frame reuses the left-eye frame's head-level view (head pose + game camera, per override site) and adds only its eye offset; the left image is held in a private texture and both eyes are copied to their swapchains together when the right eye is done, so the headset always shows a matched pair (updated at half the game frame rate, reprojected by the runtime). Cost: one extra frame of latency. Moving objects still differ by one frame until game time is frozen on the right-eye frame (next step).
Verified at the menu without a headset: left/right 50/50, 0 unmatched. Headset check pending (PLAYTEST-2a rerun). The user's config was set back to stereo.mode = alternate.

### D-020 — Full headset field of view, steered by measurement
Date: 2026-10-13  Stage: 2
Found: the render camera keeps a FOV at +0xe4 (radians; 1.222 by default) and a reference aspect at +0xf4 (1.3333), set by exe+0x3536a90 and exe+0x35369e0 (the latter is called right after the gameplay SetCameraData with [mgr+0x20]). The projection the engine builds from them did not follow one formula across three probes (FOV 1.222 / aspect 1.333 → 55° vertical; 1.571 / 1.0 → 90°; 1.85 / 1.333 → 121°), so the FOV is not computed open-loop.
Chose: with a headset (and rendering.full_fov on, F9 toggles, saved), the target is the symmetric vertical FOV covering both eyes: tan = max over eyes of tan(left/right/up/down) (the 16:9 viewport then covers the horizontal). The FOV setter is hooked for the render camera; each frame after SetCameraData the commanded tan(FOV/2) is scaled by target / rendered (vertical projection scale from the view constants) when they differ by >0.3%, waiting 6 frames between steps. Aspect is left to the game. Off / no headset → the game's FOV is restored.
Verified without a headset (diagnostics.fov_test, stand-in 90°): first guess rendered 73.7°, one step later 90.0° and stable. The OpenXR layer already submits the FOV the game actually rendered, so the picture stays correct while steering.
Trade-off (told to the user): the same 1920x1080 image now spans about 129°x100° instead of 86°x55°, so it is less sharp; render scale comes next. Asymmetric per-eye projection (a few degrees saved) is deferred.
Also fixed: head_pose's default eye FOV was zero-initialised (only mattered without a headset).

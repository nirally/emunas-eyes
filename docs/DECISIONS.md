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

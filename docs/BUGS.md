# Bug Log

Severity: 1 = crash, hard lock or nausea · 2 = feature broken · 3 = wrong behaviour with a workaround · 4 = polish/performance note.
Bugs stay open until a playtest verifies the fix.

### BUG-001 — Launch via Steam sometimes doesn't start the game
Status: open (not a mod bug) | Severity: 4 | Stage: 0 | Reported: agent, 2026-10-13
Repro: close the game, relaunch from Steam within ~1 minute.
Expected: game starts. Actual: EA app wasn't running; nothing launched for 4 minutes. A second launch worked.
Evidence: process watch 01:10 — EADesktop only appeared on the second launch.
Hypothesis: the EA app shuts down with the game and the next Steam launch races its startup. Happens with or without the mod.
Workaround: launch again.

### BUG-002 — Head-tracking toggle unusable on 60% keyboards / with Num Lock off
Status: closed (verified PLAYTEST-1e/1f: F-keys used, F5 toggled) | Severity: 3 | Stage: 1 | Reported: PLAYTEST-1a item 6
Repro: press Numpad0 with Num Lock off, or on a keyboard without a numpad.
Expected: head tracking toggles. Actual: nothing (no "override OFF" in the log).
Hypothesis: Num Lock off makes Numpad0 send VK_INSERT; and 60% keyboards have no numpad.
Fix: F7 toggles (also Numpad0 and Insert); F6 recentres (also Numpad5 and Clear). Verified by injected keys at the menu.
Verified: pending PLAYTEST-1b

### BUG-003 — Player feels short in VR
Status: closed (user set +0.14 m, PLAYTEST-1b; auto-calibration follow-up in Stage 7 calibration) | Severity: 3 | Stage: 1 | Reported: PLAYTEST-1a comfort
Expected: eye height feels like your own. Actual: "I feel really short."
Hypothesis: the camera sits at Faith's eye height, which is lower than the player's real eye height. The mono picture with no stereo depth may add to it.
Fix (step 1): comfort.height_offset, adjustable live with F3/F2 (2 cm steps) and saved automatically; the headset's real eye height above the floor is logged every 30 s. Step 2 (after data): auto-calibrate the default from real eye height vs Faith's.
Verified: pending PLAYTEST-1b

### BUG-004 — Head roll lost in gameplay (kept at the menu)
Status: closed (verified PLAYTEST-1c..1f: 0 roll assertions, user reports head tracking correct) | Severity: 2 | Stage: 1 | Reported: PLAYTEST-1a log analysis
Evidence: menu frames rendered roll == head roll (9.1/9.1); gameplay frames rendered ~0 roll vs head 1-3°.
Hypothesis (confirmed by disassembly, D-012): in gameplay a second step (exe+0x31a43b9, after helper 0x319c0e0) rebuilds the render view from a derived camera whose "up" comes from world up, dropping head roll.
Fix: hook that call site too; if the derived camera matches our view within 15°, give it our exact rotation and position; otherwise pass through and count it.
Verified: pending PLAYTEST-1b (log shows "render-view build: N frames given our view back")

### BUG-005 — View "snaps" between head pose and the game's camera in gameplay
Status: closed (verified PLAYTEST-1c) | Severity: 2 | Stage: 1 | Reported: PLAYTEST-1b ("snapping to my position too firmly")
Evidence: render-view build counters, e.g. 49 replaced vs 425 passed through in 10 s; passed frames were 30–97° from our view.
Hypothesis (confirmed by the counters): the render-view step's derived camera is built from the game's own camera, not ours. The BUG-004 fix only stepped in within 15° of our view, so with the head turned the game camera won → alternating frames.
Fix: also replace the derived camera when it matches the game camera (within 10°). Only genuinely different cameras pass through. Counters now split "from game" / "from ours" / "other".
Not done: head-pose smoothing (user asked). Declined for now: smoothing the head is the main cause of VR sickness and the symptom is explained by this bug. Revisit if smoothness still feels wrong after the fix.
Verified: pending PLAYTEST-1c

### BUG-006 — View flips backwards during Faith's roll
Status: closed (verified PLAYTEST-1d) | Severity: 1 (sudden 180° view flip = nausea risk) | Stage: 1 | Reported: PLAYTEST-1c
Repro: roll (landing roll or dodge roll).
Expected: heading stays steady through the roll. Actual: view glitches and flips backwards.
Hypothesis: the game camera pitches through straight down and beyond during the roll; the heading was read from the forward vector, whose horizontal direction flips 180° once pitch passes vertical.
Fix: heading read from the camera's right axis (unchanged by a forward roll), with forward as fallback only for extreme sideways tilt. Unit check tumbles the camera through 360° of pitch.
Verified: pending PLAYTEST-1d

### BUG-007 — In fights the player feels "in front of" Faith; only knuckles visible when punching
Status: partly fixed (PLAYTEST-4e: fights "better" with room-scale); arms/knuckles framing → Stages 5–7 | Severity: 3 | Stage: 1 (framing) → Stage 4 room-scale; arms Stages 5–7 | Reported: PLAYTEST-1d
Hypotheses, in order: (1) the mono picture uses the game's 55° vertical FOV, so arms at the edges are cut off (Stage 2 gives the headset's full FOV); (2) camera +0.14 m above Faith's eyes (height offset) moves the viewpoint up and over the arms; (3) physical head offset from the recentre point moves the camera ahead of Faith's head. Head offset is now logged every 2 s (debug) to check (3).
Workaround: F6 while standing naturally; try F2 a few steps to see whether the arms frame better.
Structural fix: Stage 5–7 (physics hands + IK: Faith's arms follow the real hands; punches are physical, user priority).
Evidence (PLAYTEST-1e log): head 0.28–0.34 m forward of the recentre point for ~20 s → camera ahead of Faith's head. Hypothesis 3 confirmed as a contributor. Stage 4 (roomscale: body follows the real body/push-back) is the real fix; F6 meanwhile.

### BUG-008 — Camera "a bit snappy" entering a roll, on pole swings and MAG Rope swings
Status: closed (verified PLAYTEST-1f) | Severity: 2 | Stage: 1 | Reported: PLAYTEST-1e
Hypotheses: (1) our view was composed from the camera manager's raw camera, which jumps on movement-mode switches; the game's render-view step blends these (that's what flat players see). We replaced the blended camera with one built from raw. (2) The look-pitch baseline (game camera motion) reset in one step whenever the camera looked calm, e.g. at a swing's apex, so the swing pitch vanished and reappeared.
Root cause (PLAYTEST-1f log): hypothesis 2. Hypothesis 1 disproven: "max blend 0.0 deg" in every window; the render-view camera equals the raw one.
Fix: at the render-view step, compose directly from the game's blended camera (separate state per site). The baseline now follows at most 0.35 rad/s, never a step. The log now reports how much the game blends ("max blend").
Verified: pending PLAYTEST-1f

### BUG-009 — Mod log went silent for a whole session while the mod worked
Status: monitoring (no recurrence: PLAYTEST-2d had one process and a complete log; the per-process fallback has not been exercised yet) | Severity: 3 (lost playtest evidence) | Stage: 2 | Reported: agent, PLAYTEST-2c log review
Evidence: the 17:02 launch (the only one; user confirms full 3D worked all session) left mecvr.log with 3 lines (start, exe version, config) and nothing after; mecvr.prev.log was still yesterday's, so no other launch rotated it. Every line is flushed and log_level was debug.
Hypothesis: two processes loaded the mod (launcher stage or a self-restart after the DSR fullscreen resolution change). The first opened mecvr.log deny-write and stopped early; the real game process could neither rotate nor open it, so it ran with no log.
Fix: if mecvr.log is held, log to mecvr.<pid>.log instead; startup now logs parent pid and command line to tell the processes apart.

### BUG-010 — Faith's body stutters/flickers when looking down, with eye sync on
Status: closed (verified PLAYTEST-2e: "no you fixed it") | Severity: 2 (flicker near the eyes) | Stage: 2 | Reported: PLAYTEST-2d
Repro: eye sync on (D-021), look down at Faith's body, especially while running. Gone with Shift+F4 (sync off).
Evidence (agent, without headset, diagnostics.zero_ipd so both eyes share a viewpoint; 4-frame PAIR captures while running): held left->right steps had the world pixel-identical but Faith's legs/torso in a different pose (1.27%, 0.61% of pixels, all in the body; docs/recon/time/pairs/). The right-eye body matched neither the previous nor the next frame.
Root cause: the game treats a frame with dt exactly 0 specially for the player body. Same test with the held frame reporting 0.1 ms: held steps 0.00-0.01% (identical); 1 ms also identical.
Fix: held frames report dt 0.1 ms (diagnostics.sync_hold_dt), given back on the next frame so game time stays exact; exactly 0 kept while the game is paused. Verified: held steps 0.00-0.01% over 6 sequences; simulation +300 ticks per 10 s.

### BUG-011 — Hand markers only flash; per-eye TAA almost never runs in the headset
Status: fixed in 5f (markers steady), see BUG-012 / BUG-013 | closed 2026-10-16 | Severity: 2 | Stage: 5 (latent since Stage 2) | Reported: PLAYTEST-5a
Repro: headset session, gameplay, look at a controller.
Expected: markers at the controllers every frame. Actual: "I saw it flash for a split second"; nothing otherwise.
Evidence: the hands are drawn right after the game's TAA pass (D-031). In every headset log since PLAYTEST-2f the TAA pass is recognised only 0-8 times per 10 s (it should be ~one per frame, ~900); on the desktop (simulator, same graphics settings and the same resize sequence) ~1,240 per 10 s. So per-eye TAA (D-023) has effectively been off in the headset too; it didn't show as a visible problem.
Hypothesis: in headset sessions the pass runs with another pixel shader than the first one learned (the old code only took that one), or its target formats differ.
Fix attempt: any pixel shader with the pass's inputs/outputs is taken (each logged); the 10 s TAA line now counts 5-target sets, other formats (+ the last ones seen), other contexts, and unmatched inputs. The desktop shows one shader and every 5-target set matched (rebuilt, 43 tests pass, deployed).
Round 2 (PLAYTEST-5b, not fixed): the targets are set once per frame with the right formats and inputs, but the draw that follows almost never reaches the check with the targets still matched. The log now names what came between: another target set (with the calling module+offset and its target count) or an indexed/instanced draw. Desktop: none of either.
Round 3 (PLAYTEST-5c): the game binds the pass's targets and then binds others without drawing (no Draw/indexed/instanced in between), from its generic bind helper exe+3617fc0. Not the FOV (desktop with 90 deg: fine). The trace now also records indirect/auto draws, clears, region copies, predication and every target bind with its caller; the mod takes one trace by itself when the pass is missed on most frames (PLAYTEST-5d).
Round 4 (PLAYTEST-5d trace): the cause is the hooks, not the game - in the headset almost none of the game's draws reach the context hooks (628 binds, 7 draws in two frames vs ~5,400 draws on the desktop). Thread-safe mode swaps the context's methods in place; both versions are now hooked (guarded against the safe version calling the plain one), and the log reports the headset's context state every 10 s (PLAYTEST-5e).
Round 5 (PLAYTEST-5e, still only flashes): the game's draws went through method versions never seen in the context's table. The runtime keeps more copies of the table in its read-only data; all of them (13 on this PC) are now found and hooked, including Map / Unmap / UpdateSubresource (the view-constant uploads that tell the eyes apart). Desktop with the headset-style thread-safe mode: TAA pass caught ~965 per 10 s (was 0-8), eye known on every one (was 0). PLAYTEST-5f.

### BUG-012 — Per-eye TAA ghosts the whole world while the head turns
Status: fixed on the desktop (D-034); per-eye TAA stays off until a headset A/B | Severity: 2 | Stage: 2 (latent; first active in the headset in PLAYTEST-5f) | Reported: PLAYTEST-5f
Repro: rendering.taa_per_eye on, turn the head (desktop: simulator yaw, numpad 6).
Expected: sharp world. Actual: "the whole world ghosting and overall more blurry"; desktop PAIR while turning shows stepped ghost copies (sharpness 5.0 vs 5.4 with it off).
Evidence: VELOCITY while turning (eye 0): the game's motion vectors -34.9 px, after the mod's correction -0.9..-3.2 px, although this eye's previous camera sits 12.4 px (x2 = 24.8 probe px) from the current one. So the correction cancels the turn and TAA blends the old frame in unshifted. Still scenes are fine (0.00 px, D-023) - only camera motion exposes it.
Cause (partial): the correction assumes the engine's motion pass measures against the other eye's latest camera; while turning it doesn't (it behaves like an older camera - eye sync's held frame probably doesn't advance it). Next: read the motion pass's real previous camera from its constants (ring buffer) instead of assuming, then re-verify turning on the desktop before any headset time.
Cause (found with BUG-013): floats 72..87 of the view constants are not "this frame without jitter" but the PREVIOUS frame's camera (= the other eye). The correction mixed up cameras one frame apart; standing still they coincide. Fix (D-034): this frame's unjittered camera is rebuilt from 40..55, the engine's previous camera is taken from 72..87 of the same frame. Desktop while turning: game vectors -40.9 px, corrected -30.0 (eye separation removed, the turn kept; before -35 -> -1), no ghost copies; sharpness 5.27 on vs 5.43 off.

### BUG-013 — Hand markers seen twice in the headset
Status: closed 2026-10-16 (PLAYTEST-5h: one box per controller, on the handle, pointing right) | Severity: 2 | Stage: 5 | Reported: PLAYTEST-5f
Repro: headset, look at a controller.
Expected: one box per controller. Actual: "I see them twice, both follow the controllers but only one is in the correct location and the other near it".
Evidence: desktop (simulated hands, real eye separation): one box per eye frame, both eyes drawn, the shift between the eyes goes the same way as nearby geometry. 5f also had per-eye TAA active (BUG-012).
Hypotheses: H1 a side effect of BUG-012 (now off); H2 one eye's box is off (depth/scale) - 5g asks which eye's box is right with one eye closed.
PLAYTEST-5g: still twice with per-eye TAA off (H1 out). Desktop measurement (new [hands] log lines): the box landed at screen x +0.27 in the left eye and +0.37 in the right - the wrong way round for a point 35 cm away; each eye drew it from the other eye's camera (63 mm to the wrong side). Cause: the matrix read as "this frame's camera" (view constants 72..87) is the previous frame's. A near box with swapped eye cameras needs the eyes to diverge - impossible, so it is seen double; the world itself was drawn correctly. Fixed (D-034): left eye +0.37, right +0.27, disparity +145.8 px = one box at 0.35 m; self-check: rebuilt camera = next frame's "previous" to 0.00003.

### BUG-014 — Hands gone (game flat) after quitting with Alt+F4
Status: closed 2026-10-16 (PLAYTEST-5k retry: hands back, 3D) | Severity: 2 | Stage: 5 | Reported: PLAYTEST-5k
Repro: quit the game with Alt+F4, start it again.
Expected: same 3D view and hands. Actual: "the hands disappeared"; log: no "stereo ON", presented frames all mono, config stereo.mode = "mono".
Cause: F4 is the mod's stereo toggle and it ignored Alt, so Alt+F4 switched stereo off and saved it to config.json (the agent's desktop test quit with Alt+F4 just before the playtest). Hands only draw in stereo.
Fixed: F4 / Shift+F4 ignored while Alt is held; the user's config set back to alternate.

### BUG-015 — Faith's body leaves the view while punching or ziplining
Status: closed 2026-10-16 (PLAYTEST-6b: no drift in fights; view within 0.21 m of Faith) | Severity: 3 | Stage: 4/5 | Reported: PLAYTEST-5m
Repro: lean or step (room-scale walks Faith), then within 1.5 s punch (attack lunge) or launch onto a zipline.
Expected: the view stays on Faith. Actual: "faith body disconnects from the headset head and I have to wait until it comes back or press R3" (recentre).
Cause: room_model absorbed all of Faith's movement along the last walking command for 1.5 s (D-029 update) with no speed limit, so a game-driven lunge/zipline along that axis was taken as our walking: the view stayed put while her body moved on (up to the 0.5 m cap), then room-scale walked her back.
Fixed: movement along the command faster than 3.2 m/s (our walking is at most 2.5) is never absorbed and ends the command; averaged over a few frames, room-scale frame time now from the precise timer. Unit tests: a 6 m/s lunge after a lean moves the view with her (only the first frame, 6 cm, absorbed); walking at 1.5 m/s still absorbed. Log: "[room] Faith moved faster than walking ... view goes with her". Not reproduced on the desktop (needs real head movement).
Reopened (user, before PLAYTEST-6b: "Faith's body still disconnects in fights; I wait or press R3"). PLAYTEST-6a log: the lunge release fired 11 times at 21:23:36, recentre at 21:23:41 - the fast part was caught, the slower attack steps (< 3.2 m/s) along a walking command were still absorbed.
Fix 2: (a) only the movement our command explains is absorbed: expected speed = commanded speed reached with 0.25 s lag, absorb at most 1.5 x that + 0.2 m/s along the command; the rest moves the view with her. (Capping at the gap to the head was tried and rejected: it broke the step-following test - the view left the real head when she coasted.) (b) For 1 s after any attack (punch, hook, flick, X/Y button) room-scale neither walks her nor absorbs. Unit test: lean 10 cm + 2 m/s attack step for 0.6 m -> view 6 cm behind her (was up to 50 cm). Log: the 2 s debug line now shows "view from Faith" to measure it in the next session.

### BUG-016 — View stays off Faith's body for seconds while standing (room-scale stops closing the gap)
Status: fixed 2026-10-16, confirm in PLAYTEST-6d | Severity: 2 | Stage: 4/6 | Reported: PLAYTEST-6c ("Faith's body disconnects from the head sometimes")
Evidence: PLAYTEST-6c.log, new "view from Faith" line: 0.1-0.4 m for 6-12 s with the head still.
Cause: BUG-015 fix 2 (a) - absorbing only the walking the command "explains" (0.25 s response model). The real game accelerates Faith much faster, so most of her walking wasn't absorbed: the view moved with her and the gap never closed. Desktop A/B (ROOMTEST, docs/recon/room_explain_ab.log): with (a) a 0.5 m step left a 0.22-0.35 m gap and Faith wandered 1.5 m; without (a) the gap closed to 0.03 m and she moved exactly 0.5 m.
Fix: (a) off (diagnostics.room_explain, default false). Kept from BUG-015: the 1 s pause after attacks (which is what fixed fights in 6b) and the >3.2 m/s release. New [room] debug line every 2 s: gap, stick, expected/actual speed.
Part 2 (PLAYTEST-6d: "yes when fighting, not frequently"): new [room] log shows the walking stick 0.75-0.78 and Faith at 0.00 m/s for 6+ s in fights (gap 0.12-0.18 m) - the game ignores the small stick in combat stance. Fix: stall push - asked to walk but not moving for 0.3 s -> the stick goes further (+0.3/s, up to +0.2, max 0.92) and stays pushed until that walk ends. Unit test with a fake combat deadzone at 0.79: gap closes (was stuck at 0.15 m); normal step unchanged. The [room] line shows "push".

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
Status: closed 2026-10-17 (PLAYTEST-6g) | Severity: 2 | Stage: 4/6 | Reported: PLAYTEST-6c ("Faith's body disconnects from the head sometimes")
Evidence: PLAYTEST-6c.log, new "view from Faith" line: 0.1-0.4 m for 6-12 s with the head still.
Cause: BUG-015 fix 2 (a) - absorbing only the walking the command "explains" (0.25 s response model). The real game accelerates Faith much faster, so most of her walking wasn't absorbed: the view moved with her and the gap never closed. Desktop A/B (ROOMTEST, docs/recon/room_explain_ab.log): with (a) a 0.5 m step left a 0.22-0.35 m gap and Faith wandered 1.5 m; without (a) the gap closed to 0.03 m and she moved exactly 0.5 m.
Fix: (a) off (diagnostics.room_explain, default false). Kept from BUG-015: the 1 s pause after attacks (which is what fixed fights in 6b) and the >3.2 m/s release. New [room] debug line every 2 s: gap, stick, expected/actual speed.
Part 2 (PLAYTEST-6d: "yes when fighting, not frequently"): new [room] log shows the walking stick 0.75-0.78 and Faith at 0.00 m/s for 6+ s in fights (gap 0.12-0.18 m) - the game ignores the small stick in combat stance. Fix: stall push - asked to walk but not moving for 0.3 s -> the stick goes further (+0.3/s, up to +0.2, max 0.92) and stays pushed until that walk ends. Unit test with a fake combat deadzone at 0.79: gap closes (was stuck at 0.15 m); normal step unchanged. The [room] line shows "push".
Part 3 (PLAYTEST-6e: still "sometimes"): the stall push works but the log showed the remaining cause - steps/leans while the player drives with the stick (or in the 1 s after an attack) are never corrected (D-029: the player's stick wins), adding up to 0.34 m in a fight. Fix: while the player drives, the view slides back onto Faith's body at 0.25 m/s (above 4 cm). Also: no walking stick while the game is paused / in menus (the full push went out for 30 s at session start). Unit test: 0.3 m gap while running -> < 5 cm in 1.5 s.
Part 4 (PLAYTEST-6f: "yes but much better, less frequently"; slide barely noticed; no drift; comfort 5): PLAYTEST-6f.log has 4 samples over 0.12 m, all right after a lean/dodge in a fight with the stick at 0.87-0.92 (push on) and Faith 0.00 m/s for 2-4 s, max rumble nearby (taking hits, or her body against the enemy) - the game ignores even the pushed stick then; the gap closed only after 4-6 s. Fix: stalled 0.8 s -> the view slides onto her at 0.4 m/s (stuck_s / stuck_bleed); 0.35 m closes in ~1.7 s. Side effect: standing against a wall the view now slides back onto her body too (still never through it). Unit tests: never-walking Faith, 0.35 m gap -> < 5 cm in 2 s; wall test updated. The [room] line shows "stuck: view slides".
Closed 2026-10-17 (PLAYTEST-6g: "1 no, 2 yes, 3 ok, 4 5" - no separation, slide OK, wall slide OK, comfort 5). PLAYTEST-6g.log: largest gap 0.29 m during 8 deliberate dodges in 8 s, closed within ~4 s; "stuck: view slides" 4 times.

### BUG-017 — Punches and kicks stopped working with the 6D gadget gestures on
Status: closed 2026-10-17 (PLAYTEST-6j) | Severity: 2 | Stage: 6 | Reported: PLAYTEST-6h ("punches and kicks stopped working", "broken from the start"; Disruptor didn't work, no target / not sure it's unlocked)
Evidence: PLAYTEST-6h.log: in the 12:17 fight 6 "throw -> MAG Rope" right after hooks (12:17:03.014 hook, .025 throw) and 5 "open-hand push -> Disruptor" instead of Light attacks (also 12:14:40 in the first fight).
Cause: D-041 limits too loose for fighting. (1) Throw: >30 deg up ending at chin height matched hooks from a raised guard; the hold then lasted until the arm went below the shoulder - a guard never does, so Up was held up to 4 s and that hand's punches were dropped meanwhile. (2) Open hand: "grip loose" (others < 0.35) matched relaxed fists - punches became B.
Fix: throw >45 deg up ending 8 cm above the eyes, no throw within 0.4 s of a hook from that hand, let go when the hand is back below the eyes (-5 cm), max 3 s. Open hand needs the grip released (others < 0.2) for 0.3 s before the push; combat.disruptor_push default off until there is a Disruptor target to tune on. 3 new unit tests (guard hook, throw after hook, guard height lets go).
Part 2 (PLAYTEST-6i: "1 yes but sometimes punches register as kicks, 2 yes but sometimes it's doing a kick, 3 no, 4 no, 5 5"): punches are back. PLAYTEST-6i.log: (a) every throw that kicked had "flick: attack (door slam)" 15-40 ms before it - the throwing hand met the wall/structure at 4-10 m/s, and the 5d flick sent Heavy at once; (b) ~30% of swings were "hooks" (Heavy = kick) - the hook test was only "over 50 deg off the view", so straight punches drifting outwards counted; (c) 11 open-hand pushes sent B for 120 ms, nothing happened at the target.
Fix: (a) the flick's attack goes out 100 ms late and a throw cancels it (hand_pose::cancel_flick; the throw log says "wall flick dropped"); (b) a hook must swing across the body, towards the other side (lateral direction > 0.5 inwards); everything else forward within ~60 deg is straight; (c) the Disruptor push holds B 500 ms. Unit test: outward-drifting right punch = straight, left hand swinging right = hook. Confirm in PLAYTEST-6j.
Closed 2026-10-17 (PLAYTEST-6j: "1 yes, 2 no" - straight = punch, hook = kick; no kicks from MAG Rope throws; comfort 5). PLAYTEST-6j.log: "wall flick dropped" twice on throws.

### BUG-018 — Body still sometimes comes apart from the head (after BUG-016)
Status: open | Severity: 3 | Stage: 4/6 | Reported: PLAYTEST-7a ("the body still sometimes disconnects from the head"; comfort 5)
Evidence: PLAYTEST-7a.log [room] lines (every 2 s), 5 samples over 0.15 m in ~3 min, all with the walking stick out ("walking", stick 0.8-0.9): 17:08:13 0.24 m and 17:08:19 0.28 m while Faith ran 4-6 m/s (the "faster than walking: view goes with her" release firing ~160 times then); 17:09:02 0.48 m appearing within 2 s with the head still (head offset moved 5 cm), right after "right hand touched something -> Interact held" and looking 61 deg down, closed by 17:09:04 ("stuck: view slides"); 17:09:26 0.36 m 4 s after a hook, Faith 0.93 m/s. Each closed within 2-4 s.
Hypothesis: the game moving Faith on its own (interact / vault, knockback, running starts) at walking speed is read as unabsorbed room-scale gap; the 2 s log sampling can't tell. Next: log the gap's onset - done 2026-10-18: [room] "view came off Faith" (gap, head and Faith movement that frame, stick, push, attack age) and "view back on Faith after N s". Read it after PLAYTEST-7b.

### BUG-019 — Two fingers of her left hand stretched towards the ground
Status: closed 2026-10-19 (PLAYTEST-7d: "4 no") | Severity: 3 | Stage: 7 | Reported: PLAYTEST-7b ("the pinky and forth finger on the left hand has stretch skin towards the ground")
Evidence: PLAYTEST-7b.log: "arm 1 (her left): hand 22" bones placed (earlier sessions 25-26). Unplaced bones keep the game's pose - her game hand is low, so the skin stretched down.
Cause: arm_solve learns from the first 400 frames only; with her arms nearly still in that window joints are poorly determined. Desktop showed worse: a solve with upper arms 0.37/0.40 m and 2 hand bones was taken as "ok" (two chains were never checked against each other).
Fix: (1) arm_solve::implausible - both arms of a human length (upper 0.20-0.36, forearm 0.18-0.32 m), within 3 cm of each other, 6+ hand bones, else the solve fails and learning retries (no longer stops after 5 tries). (2) Learning goes on after the IK starts: 400-frame windows solved on their own, 150 windows (~10 min); bones the first solve left off that two windows place the same way join (arm_solve::adopt_new_bones: arms matched by 90%+ of the bones both placed; nothing placed changes). Unit tests: 3 new. Desktop: first solve correct on try 3 (tries 1-2 rejected), windows that found arms all agreed, none disagreed; the desktop never misses hand bones, so the adding itself is only unit-tested. Log: "arms again, windows N-M" every 10 windows, "N more bones placed".
PLAYTEST-7c.log: first solve ok on try 1 with hand 25 and 26 bones (the 7b session had 22); windows 1-10: 9 agreed, 0 other arms. Close after the user confirms in PLAYTEST-7d.

### BUG-020 — Right palm glitching (glove/wrist wrap flicker)
Status: closed 2026-10-19 (PLAYTEST-7d: "3 no") | Severity: 3 | Stage: 7 | Reported: PLAYTEST-7b ("on the right hand the palm of the hand is a bit glitching")
Evidence: PLAYTEST-7b.log: 18-144 frames per 5 s with no stencil-marked Faith piece in normal play (0 on the desktop), "0 of them before the arms' own draw".
Cause: the unmarked path (D-045) switched on one frame late (after a frame without marks) and found the arms only at their own draw - her right glove and wrist wrap, drawn before the arms, kept the game's pose in those frames.
Fix: no prediction - once the IK runs every skinned draw of 3+ bones is taken each frame; the IK runs at the earliest draw that can find the arm block (her first marked piece, else a piece whose offset to the arm block was learned - now in marked frames too). Her shadow-pass copies follow too now (~230 rows a frame patched, was ~72; fps unchanged on the desktop). One IK per frame (an early find no longer gets recomputed at her first marked piece).
Fix 2 (after PLAYTEST-7c, 2026-10-19): a second cause found in a desktop recording (docs/recon/stage7/pieces_rec_4.bin, every marked piece's rows over 1500 frames): her right glove piece has a helper bone of its own, 6 mm from her hand bone and rigid with it to 3 mm, that is not a copy of any arm bone - left in the game's pose, it pulled the palm. In a piece where half or more of the rows are copies of arm bones (glove, wrist wrap), every other row within 3 cm of a copied bone now takes that bone's correction (arm_ik::carry, unit-tested). Torso and leg pieces are left alone. Desktop: ~10 such rows a frame, no errors, fps unchanged. Log: "(N riding with a nearby arm bone)".

### BUG-021 — Faith climbs on her own while hanging or on a pipe; arms glitch when the head moves
Status: closed 2026-10-19 (PLAYTEST-7e: "1 no"; arm glitch left -> BUG-022) | Severity: 2 (the game acts without the player) | Stage: 4/7 | Reported: PLAYTEST-7d ("when moving my head the arms glitch and climb on it's own"; "on the pipe faith's body acts on it's own and start climbing up"; ladders not tried)
Evidence: PLAYTEST-7d.log 19:38:48-19:38:52 (hanging): [room] "walking, stick +0.17 -0.90 / -0.48 -0.63, Faith 0.00 m/s" - room-scale (D-029) pushed the move stick to walk Faith under the real head. On a ledge, pipe or ladder the stick climbs or shimmies her instead, and she doesn't move horizontally, so the gap never closed and the stall push grew. The hang detection switched off and on 3 times in 6 s (each climb step drops her hands for a moment) - the arm "glitch".
Fix: (1) faith_bones notes every frame either of the game's hands is 10+ cm above its shoulder (hand_up_at). For 0.6 s after that, room-scale counts as "held" (as while the player drives): no walking stick, and the view slides back onto her body at 0.25 m/s. (2) Arms go back to following yours only after her hands have been down 30 frames (was 6).
Desktop: standing 0 hand-up frames. A real ledge hang (she backed off an edge) - room line "(hands up: hanging/climbing)", stick 0. Pulled up - the hold ended, arms followed again, room-scale normal. Falls count as hands up (harmless). Not cleared: a long run on flat ground (both runs on that roof ended over an edge); a raised hand while running only pauses room-scale while the player's own stick is in use anyway. Log: [bones] "N frames a game hand above the shoulder", [room] "N climb holds".
Result: PLAYTEST-7e - no climbing on her own on ledges/pipes; room-scale on the ground as before; comfort 5. Log: 39 climb holds, stick 0.00 in every [room] line while hands up.

### BUG-022 — Faith's arms glitch while climbing pipes and walls (one game hand up at a time)
Status: closed 2026-10-19 (PLAYTEST-7f: "1 no, 2 yes no") | Severity: 3 | Stage: 7 | Reported: PLAYTEST-7e ("2 yes but less frequently")
Evidence: PLAYTEST-7e.log 20:07:03-20:07:28: 93-165 frames per 5 s with a game hand above the shoulder but no "hanging" at all - the hang pose (D-045 follow-up, PLAYTEST-7c) needed BOTH hands up 30 frames in a row, which pipes and wall climbs (hands in turn) rarely give. Hangs that did start lasted 0.2-0.7 s. Meanwhile the IK put her arms on the controllers while the climb animation moved her shoulders: up to 497 of 816 arm poses per 5 s out of reach (stretched, clamped).
Fix: the game's arms take over when EITHER hand is up for 30 frames, with dips of up to 8 frames (between climb steps) not breaking the run; back to yours after 30 frames with no hand up (faith_bones kGapFrames).
Desktop: running, jumps and vaults on the start roof - 0 hand-up frames, arms on the simulated hands throughout (no false handover). No pipe or ledge reachable there: climbs untested on the desktop.
Result: PLAYTEST-7f - no glitch while climbing/hanging, arms follow yours on the ground, no wrong handovers, comfort 5. Log: 23 handovers, all ended; arm poses clamped 5.3% (7e: 11.9%).

### BUG-023 — Faith's whole body glitches (first 7D build)
Status: closed 2026-10-19 (PLAYTEST-7g retry: "5 not so far", comfort 5) | Severity: 2 (unplayable, test abandoned) | Stage: 7 | Reported: PLAYTEST-7g ("faith's whole body is glitching so I did not test anything")
Evidence: PLAYTEST-7g.log: that session's arm solve was unlike every earlier one - 28 joints (usually 37-53), upper arms of 5 bones and forearms of 5 (always 4 and 6 before); rows of other pieces moved with the arms +25%, rows riding with a nearby arm bone 7,000-18,000 per 5 s (7f: 400-2,900). The solver puts a bone on the upper arm when it pivots at the shoulder, and leaves it off only if a joint of it past the shoulder shows; with fewer joints found, a chest bone most likely joined the upper arm, so every piece of her body that has that bone swung with your arm. Recording check (bones_rec.bin): the shoulder/chest bones 14, 15, 16, 18, 19, 22, 24, 41 all put her elbow 16-23 cm from where the upper arm bones (13/20, 56/58) put it - none rides with the arm; 14/16 and 19/41 had been on the upper arms in every session (shoulder skin only, unnoticed).
Fix: arm_solve keeps a bone that pivots only at the shoulder on the upper arm only if it keeps the elbow within 2 cm of the upper arm bone's (Learner::apart; a twist bone turns about the line through the elbow). Test with a rig whose collarbones show no breastbone joint (failed before: both collarbones on the upper arms). The solve's upper arm and forearm bones are now logged. Also: finger_rig takes finger bones the solver put on the forearm (desktop solves from little motion do).
Desktop: upper arms now 2 bones each (13 20 / 56 58), fingers on both hands, rows riding 400-500 per 5 s. The bad solve itself couldn't be reproduced on the desktop.

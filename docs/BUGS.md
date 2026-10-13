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
Status: open | Severity: 3 | Stage: 1 (framing) → resolved structurally by Stages 2, 5–7 | Reported: PLAYTEST-1d
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

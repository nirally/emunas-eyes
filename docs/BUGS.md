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
Status: fixed, awaiting playtest | Severity: 3 | Stage: 1 | Reported: PLAYTEST-1a item 6
Repro: press Numpad0 with Num Lock off, or on a keyboard without a numpad.
Expected: head tracking toggles. Actual: nothing (no "override OFF" in the log).
Hypothesis: Num Lock off makes Numpad0 send VK_INSERT; and 60% keyboards have no numpad.
Fix: F7 toggles (also Numpad0 and Insert); F6 recentres (also Numpad5 and Clear). Verified by injected keys at the menu.
Verified: pending PLAYTEST-1b

### BUG-003 — Player feels short in VR
Status: mitigated, awaiting playtest | Severity: 3 | Stage: 1 | Reported: PLAYTEST-1a comfort
Expected: eye height feels like your own. Actual: "I feel really short."
Hypothesis: the camera sits at Faith's eye height, which is lower than the player's real eye height. The mono picture with no stereo depth may add to it.
Fix (step 1): comfort.height_offset, adjustable live with F3/F2 (2 cm steps) and saved automatically; the headset's real eye height above the floor is logged every 30 s. Step 2 (after data): auto-calibrate the default from real eye height vs Faith's.
Verified: pending PLAYTEST-1b

### BUG-004 — Head roll lost in gameplay (kept at the menu)
Status: fixed, awaiting playtest | Severity: 2 | Stage: 1 | Reported: PLAYTEST-1a log analysis
Evidence: menu frames rendered roll == head roll (9.1/9.1); gameplay frames rendered ~0 roll vs head 1-3°.
Hypothesis (confirmed by disassembly, D-012): in gameplay a second step (exe+0x31a43b9, after helper 0x319c0e0) rebuilds the render view from a derived camera whose "up" comes from world up, dropping head roll.
Fix: hook that call site too; if the derived camera matches our view within 15°, give it our exact rotation and position; otherwise pass through and count it.
Verified: pending PLAYTEST-1b (log shows "render-view build: N frames given our view back")

# PLAYTEST-0 results — 2026-10-13
All 6 items: pass. User: "everything worked as usual."
Log: SMOKE OK at 16:29:56, no WARN/ERROR, gameplay 125–150 fps flat 1080p.
Gap (instrumentation bug, ours): export call counts were only logged for the first 2 minutes, so updateCamera use during gameplay is still unknown. Fixed in Stage 1.

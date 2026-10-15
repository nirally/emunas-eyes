# PLAYTEST-5b results — 2026-10-15

"still same issue with the boxes"
- Log: the pass's 5 targets are set ~860 times per 10 s with the right formats, one shader, inputs always matched - but only 0-3 draws were taken as the pass. So between the target set and the draw the match is lost (another target set, a draw of another kind, or the draw not reaching our hook). Headset-only (desktop: every set followed by the draw). -> BUG-011 diagnostics round 2, PLAYTEST-5c.

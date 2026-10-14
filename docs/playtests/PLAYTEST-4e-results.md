# PLAYTEST-4e results — 2026-10-14

"1 yes and yes but I would like for her to walk faster so I can't see the body walking to me, I want the body to move exactly when my head is moving so I would not see it anymore., 2 no, 3 better, 4 yes, 5 5"
- Faith walks to the player and it feels like being inside her body afterwards; no walking on her own; fights better (BUG-007); walls stop the view; comfort 5.
- Wanted: the body moving with the head, not walking over afterwards.
Follow-up (agent): Faith's speed per stick measured (0.70 -> 0.16 m/s, 0.80 -> 0.93, 0.85 -> 1.5, 0.90 -> 3.9 run; coast 0.15 m after a walk, 0.5 m after a run). room_model now follows the head's velocity plus a catch-up, aims short by her momentum (no overshoot), starts at 4 cm instead of 15 cm, and absorbs her movement along the command axis (view stays exactly on the head). In game, a real-speed 0.5 m step: gap peaks 0.25 m about 0.4 s into the step (the game barely moves her for the first ~0.3 s), under 0.07 m by 0.8 s, settles ~0.04 m; before: she started at 0.15 m and took ~3 s. Exact, instant following would need writing her position directly (research; collision risk).

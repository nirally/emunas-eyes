# PLAYTEST-7a — Faith's own arms follow yours (~10 min)

New: Faith's real arms and hands now follow your controllers - her shoulders stay where the game puts them, her
hands go to yours and turn with them, her elbows bend down and out. It learns her arms in the first ~5 s of
play, so for a moment her arms are the game's. The gloved hands from Stage 5 are still drawn on top (hiding them
is question 4).

Play normally for ~10 min: look at your arms, move them around (high, low, across your body, behind you), punch,
run, climb, grab a ledge, use a zipline if you pass one. Quit normally.

1. Do her hands sit at the gloves and turn with your hands? Do the elbows look natural?
2. Anything broken: stretched or torn skin, a gap at the wrist, arms flickering or jumping, arms pointing the wrong way?
3. Climbs and ledge grabs: now her arms follow yours even there. Do you prefer that, or her hands on the ledge (the game's animation) while she climbs?
4. Now that her arms follow, should the gloves go (her own hands only)?
5. Comfort, 5 = very comfortable.

Reply example: "1 yes, elbows ok, 2 right wrist stretched when punching, 3 game's animation, 4 hide, 5 5".

## Result (2026-10-18)
User: "1 yes, elbows ok, 2 the body still sometimes disconnets from the head, 3 follow, 4 hide, 5 5".
Log: PLAYTEST-7a.log (session 17:06-17:10, ~3 min with IK). Arms learned at 17:07:55 (upper arm 0.28 m, forearm 0.25/0.26 m, left = arm 1, 60/60); then both arms posed ~70 frames/s per 5 s window, 0 hands too far, clamped (out of reach) 0-286 per window, hands posed up to 0.86 m from the shoulder; no warnings or errors.
Disconnects -> BUG-018. Q-701 = always follow, Q-702 = hide.

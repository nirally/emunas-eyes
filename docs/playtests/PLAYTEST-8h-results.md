# PLAYTEST-8h results (2026-10-26)

User: "1 yes and also the hands are glitching after the cutscne ends"

Headset log: PLAYTEST-8h.mecvr.log (session 22:18-22:22, after the 22:11 deploy of D-054). Config on arrival: stereo alternate, game motion on (no F4/F5 presses this session).

1. Train roof: still shaking.
2. Rolls/landings, comfort: not answered.
New: the hands glitch after the cutscene ends (BUG-031 back in another form?).

Log:
- Shake: the detector faded the game motion out on the roof (22:20:55-22:21:35: 308, 448, 448, 445, 268 updates per 10 s at the main camera site), and the user still saw the shake. On paper a faded update = F5 off (both: heading + the game's look pitch + head), and F5 fixed it in 8g - so either the shown view (composed again at the render-view site, with its own detector, not logged) wasn't faded, or the shake isn't in the faded part.
- Hands: after the cutscene (22:20:37-22:21:00) "forearm twist kept from flipping" 553 of 894 posed arms in one 5 s window (34-58 in others): the raw twist crossed +-180 deg nearly every frame - her own hand was turned ~180 deg from the controller's, where the swing-twist split is ill-conditioned (q.w and the twist part both near 0) and the twist is noise.

Desktop follow-up (desktop-8h-fakeshake.log, diagnostics.fake_shake 3 deg: roll 4 Hz + pitch 5 Hz): the detector never faded it - it counted a reversal only when the whole step turned past 90 deg, and a shake on two axes turns smoothly. The 8h log has such a window inside the carriage after the cutscene (22:20:35-45: 1.70 deg avg, 0 faded). Counted per axis now: fake shake faded in every update, the shown view's shake 0.00 at both the camera site and the render view (was 0.49-0.66 with only the filter, 1.19-1.60 with neither; game motion off 0.00). Running: 3 faded updates in 10 s; a fall to her death: faded (harmless).

# Open Questions

Each question says why it matters and what I'll assume if you don't answer.

### Q-001 — Does your Steam copy currently reach gameplay? — ANSWERED: yes
Why: since EA shut down the servers, some players hang on a loading screen after the intro. Stage 0 needs the game running reliably.
Default if unanswered: assume it works, since you've played it a lot. If it hangs, try blocking the game's internet access in Windows Firewall (nothing to do with the copy protection).

### Q-002 — Build path: from scratch, or hybrid? — ANSWERED: hybrid
Why: there's no existing Catalyst VR mod to build on top of. See D-001.
Default if unanswered: hybrid (my own code, plus MIT-licensed pieces and Frosty data mods).

### Q-201 — (agent recon) Does Frostbite render more than one view per frame if asked?
Why: decides rung 1 vs rung 3 for stereo. Answered by recon, not the user.
Default: try it at the menu first.

### Q-204 — Mouse look up/down: keep it, or head only? — ANSWERED 2026-10-14: (a) mouse pitch stays
Context: PLAYTEST-2d, a mouse look-down drifts back to level within a few seconds (game camera motion removes mouse pitch gradually, D-014).
Options: (a) camera.pitch_source = game: mouse pitch is added on top of the head and stays; the view can tilt away from the real horizon. (b) keep auto: the head does up/down, mouse pitch fades out (today). (c) head only, mouse pitch ignored immediately (no drift, no effect).

### Q-501 — Your hands in the game: what should they look like, and what about Faith's own arms? — ANSWERED 2026-10-14: (a) mod hands always at the controllers; Faith's arms stay until Stage 7
Context: Stage 5 puts hands where your controllers are. Faith's own first-person arms also appear in the game (climbing, ledge grabs, attacks, Gridnodes), animated by the game.
Options:
 (a) Faith-style gloved hands drawn by the mod, always at your controllers; Faith's own arms stay visible during her moves (you may see both briefly) until Stage 7 drives her arms from your hands. (Recommended: fastest to a good result; Stage 7 later merges them.)
 (b) Same hands, and hide Faith's own arms when possible (if they can be told apart in the frame; some moves may look odd without arms).
 (c) No separate hands: wait for Stage 7 to move Faith's own arms with yours (much later).

### Q-604 — MAG Rope + Disruptor by hand: how? — ANSWERED 2026-10-17: gesture, head-aimed
Options: (a) gesture fires the gadget, the game keeps choosing the target from the view (= head) (recommended, small); (b) true hand aim (reverse-engineer target choice, large/uncertain); (c) buttons only, skip 6D.
Answer: (a). Buttons stay as fallback.

### Q-605 — Which pad button is the Disruptor, and is it unlocked in this save?
Context: PLAYTEST-6j: the push gesture sends B (a sound plays), but pressing B at the target does nothing either. "B = Disruptor" in D-027 came from the assumed default layout, never verified in game.
Ask: Options > Controls (controller layout) - which button is listed for the Disruptor? Was it used successfully anywhere?
ANSWERED 2026-10-17: B is the Disruptor, but it has to be held to activate; everything is unlocked in the save. -> the push now holds B while the hand stays open (1.5-5 s).

### Q-701 — Faith's arms during climbs and grabs: the game's animation or yours?
Context: Stage 7 makes Faith's own arms follow your real arms. In climbs, ledge grabs, pipe/ladder climbs and some attacks the game animates her hands onto the world.
Options: (a) game animation during those moves, blending back to yours after (recommended: her hands land on the ledge); (b) always yours.
Default if unanswered: (a). Ask at the first IK playtest.
ANSWERED 2026-10-18 (PLAYTEST-7a): (b) always yours - her arms follow you in climbs and grabs too.

### Q-702 — Hide the mod's gloved hands once Faith's own hands follow you?
Default: hide them (one pair of hands), with an option to show them. Ask at the first IK playtest.
ANSWERED 2026-10-18 (PLAYTEST-7a): hide.

### Q-801 — Does the HUD bother you in the headset now? (health, prompts, timers, objective text)
Why: if it's readable and comfortable as it is, Stage 8 can give it a fixed depth and spend its time on menus and cutscenes.
Options: (a) fine; (b) too close / hard to focus / doubled; (c) too big or at the edge of view; (d) never noticed it.
Default if unanswered: (b) -> a panel ~2 m ahead.
ANSWERED 2026-10-26: "the HUD don't bothers me but I can't see it clearly. the health bar is cropped and I can only see the end of it. prompts work well and I did not test the rest. I would like for the health bar to be on my bottom left screen at all time without being cropped."

### Q-802 — Menus and the map (pause, options, upgrades, the big map): usable in the headset?
Options: (a) fine; (b) readable but uncomfortable; (c) hard to read; (d) other.
Default: (b) -> a floating panel in front of you.
ANSWERED 2026-10-26: "no they are zoomed in to the center so I can't see the most of the menus."

### Q-803 — Cutscenes: in 3D with your head free (as now), or on a big virtual screen?
Context: cutscene cameras move on their own; a virtual screen is the comfort-safe choice, 3D is more immersive.
Options: (a) 3D as now if comfortable; (b) virtual screen; (c) a setting, default (a).
Default: (c).
ANSWERED 2026-10-26: "keep it in 3D but add a setting to make it a big virtual screen."

### Q-804 — Health on your wrist?
Context: the HUD is one shader, so moving a single element (health) needs extra recon and may not work.
Options: (a) want it, worth the effort; (b) not needed, the whole HUD on one panel is fine.
Default: (b) first, (a) later if possible.
ANSWERED 2026-10-26: "not needed for now".

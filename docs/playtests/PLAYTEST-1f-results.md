# PLAYTEST-1f results — 2026-10-13
User: "everything worked great now!" (roll entry, pole swing, MAG Rope swing, death/respawn, optional scripted moment).
Log: 0 assertion failures; ~90 fps; render-view step composed every frame; "max blend 0.0 deg" in every window → the derived camera equals the raw camera; BUG-008 hypothesis 1 disproven, hypothesis 2 (baseline step) was the cause.
Unclassified setter caller exe+0x327724b fires occasionally (6 windows): candidate for respawn/transition cameras; not overridden (pass-through), no user-visible issue.

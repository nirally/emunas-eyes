# PLAYTEST-5d results — 2026-10-16

"1 no, 4 one hitch, 5 5"
- Auto trace (docs/recon/taa/trace_headset_5d.txt): two headset frames = 628 target binds and 7 draws; the desktop trace has ~5,400 draws. In the headset the game's draws almost all bypass the mod's context hooks; target binds still arrive. Not the game skipping the TAA pass.
- Desktop experiments: switching the device's thread-safe mode on (ID3D11Multithread, as OpenXR runtimes do) rewrites the context's method table in place (Draw ...3540 -> ...2990, 63 of 70 methods) and the hooks stop seeing the game's calls. Hooking both versions of every method (all 5 context interfaces share one table) still left the game's calls unseen after a mid-game switch, with no command lists - so that experiment isn't a faithful copy of the headset (there, binds were still seen). The thread-safe version calls the plain one: a reentrancy guard keeps the TAA logic to the outermost call.
- Next: PLAYTEST-5e with the hooks on both versions + a 10 s report of the headset's actual state (thread-safe mode, unhooked current methods, calls per version, command lists).

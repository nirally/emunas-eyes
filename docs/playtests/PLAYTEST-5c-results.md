# PLAYTEST-5c results — 2026-10-15

"1 no, 4 no, 5 5"
- Log: after nearly every 5-target set (the TAA pass's targets) the game's next call is another target set with n=1 from MirrorsEdgeCatalyst.exe+361800a - with no draw of any kind in between (887 of 890 per 10 s). exe+3617fc0 is the engine's generic "bind render targets + viewport" helper (OMSetRenderTargets = vtable +0x108, RSSetViewports +0x160; docs/recon/taa/codedump_361800a.bin), so the caller says nothing about why.
- Desktop with the headset's wide FOV forced (diagnostics.fov_test, 90 deg): pass found every frame -> FOV not the cause. Eye sync identical on desktop.
- Next: auto frame trace in the headset (PLAYTEST-5d). Desktop reference: docs/recon/taa/trace_desktop_5c.txt (om n=5 -> TAA draw directly).

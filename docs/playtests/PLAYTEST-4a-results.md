# PLAYTEST-4a results — 2026-10-14

"1 yes but I don't like it I much prefer to control faith running direction only with the joystick., 2 yes, 3 when crouching faith did not crouched with me so I couldn't pass low height obstacle and slide, 4 no, 5 no, 6 none, 7 5"
- Body follows head works but isn't wanted: the user wants the stick (and turning) to set Faith's running direction. -> input.body_follows_head, default off (the game is steered to the view heading only; head looks around freely).
- Turn speed 180 deg/s: OK. Nothing jerky, no drift or spinning. Comfort 5.
- Real crouch: nothing happened. Agent tests (PADTEST, camera height in the debug log): Catalyst has no standing or walking crouch — Down actions held while standing or walking leave the camera at the same height; running + Down actions slides (camera -0.8 m). So real crouch can only mean "slide when running". Threshold lowered 0.30 -> 0.25 m (user's eye height 1.54 m) and crouch on/off + lowest head height are now logged.
- Cutscene/takedown: none.

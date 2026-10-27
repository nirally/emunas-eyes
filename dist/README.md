# Runner's Sight

**A full VR mod for Mirror's Edge Catalyst:** true 3D, 6DoF head tracking and motion controls.
Faith's arms follow your hands, and you can punch, push with the Disruptor and throw the MAG Rope with your own arms.
Free-roam crouching and real steps also work.

Runner's Sight is a fan-made mod. It is not made, endorsed or supported by EA or DICE.
It only adds its own files to the game folder. No game files are included or changed, and uninstalling puts the game back as it was.

## Requirements

- **Mirror's Edge Catalyst, Steam version 1.0.3.47248** (the current Steam build). Other versions start in *safe mode*: the game runs flat and the mod does nothing.
- **SteamVR** set as the OpenXR runtime (SteamVR > Settings > OpenXR > "Set SteamVR as OpenXR runtime").
- **A PC VR headset with two tracked controllers.** It was developed and tested on a Meta Quest 2 with Touch controllers over Steam Link. Valve Index controllers have bindings but haven't been tried.
- **A GPU that can run the game at a high resolution.** It was tested on a GTX 1080 Ti at 2880x1620 (NVIDIA DSR) for a sharp picture.
- **Single-player only.** The game's online features were shut down by EA, and the mod doesn't touch the DRM.

**Tip:** turn off automatic updates for the game in Steam (Properties > Updates). A game update makes the mod step aside until it's updated too.

## Install

1. Close the game.
2. Extract the whole zip anywhere.
3. Double-click **Install.bat**. It finds the game through Steam.
   - If it can't find the game, open PowerShell in the extracted folder and run: `powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir "D:\path\to\Mirrors Edge Catalyst"`
4. Start SteamVR, put the headset on, then start the game from Steam.

Installing a newer version over an older one keeps your settings and profiles.

## Uninstall

Double-click **Uninstall.bat**. It removes everything the mod added and puts the game's own `NvCameraSDK64.dll` back.
Run `uninstall.ps1 -KeepSettings` to keep your settings for a later reinstall.
To double-check, use Steam: right-click the game > Properties > Installed Files > *Verify integrity of game files*.

## Controls (Meta Touch; Index the same by position)

| | Left hand | Right hand |
|---|---|---|
| Stick | Move | Turn |
| Stick click | Tap: pause · Hold 1 s: map | Tap: Runner Vision · Hold 1 s: recentre |
| Grip | Up actions (jump, climb) | Quick turn |
| Trigger | Down actions (slide, crouch) | Shift |
| X / A | Light attack | Interact |
| Y / B | Heavy attack | Disruptor |

- **Both sticks clicked in and held for 1 second:** the settings menu.
- **Y and B held together:** a screenshot from the headset, saved in the game folder's `mecvr` folder.
- **Your body:** punch to attack (a hook is a heavy attack), and push with an open hand for the Disruptor. Throw a hand up past your chin to fire the MAG Rope. Crouch for real to slide, and touch things in front of you to use them.
- **Left-handed:** in the settings menu, Controls > Left-handed mirrors the whole layout.
- **Index controllers:** the left A/B buttons take the place of X/Y.
- **The Touch menu button opens SteamVR's dashboard**, not the game's menu. Use the left stick click to pause.

## The settings menu

Click both sticks in and hold them for 1 second to open the menu.
- Sticks up/down move through it and left/right change the value. A picks, and B goes back (B on the first page closes the menu).
- Changes apply at once and are saved.
- Every section has *Reset this section to defaults*, and the first page has *Reset all to defaults*. Both ask twice.

**Profiles** (on the first page) hold 4 saved copies of your settings, for example one for standing, one for seated, and one for someone else sharing the headset.
*Save your settings as profile N* stores your settings as they are now, and *Load profile N* puts them back.

**Playing seated:** sit down, look straight ahead and hold the right stick click for 1 s to recentre.
Then raise Calibration > *Extra eye height* until Faith's view feels right, and save it as a profile.

**Comfort:** Comfort > *Feel the game's camera motion* (or F5) adds the game's rolls, wall-run tilt and landing dips to your view. It's intense, so leave it off unless you're used to VR. If you feel warm, sweaty or dizzy, stop at once and take a break.

Settings live in `mecvr\config.json` in the game folder, and you can also edit it while the game runs.
With a keyboard: F2 / F3 lower / raise your eye height, and F7 turns the VR camera off and on.

## Known issues

- Faith's body is sometimes briefly out of place relative to your head.
- In her jacket (the first mission), her fingers keep the game's pose instead of following yours.
- Runner Vision, the objective and enemy markers, and subtitle size haven't been fully checked in the headset.
- Performance hasn't been tuned. Expect to need a strong GPU for a sharp, smooth picture.
- Controllers other than Meta Touch haven't been tested.

## Reporting a bug

1. Press **F11** on the keyboard right after it happens. This writes a report with your settings to `mecvr\reports\`.
2. Note what you were doing.
3. Send the report together with `mecvr\mecvr.log` (this session) and `mecvr\mecvr.prev.log` (the one before).

If the mod stops the game from starting, create an empty file named `SAFE_MODE` in the `mecvr` folder. The game then runs flat and the mod stays out of the way.

## Credits

Made by Nirally with Claude (Anthropic). It uses MinHook (BSD-2-Clause), nlohmann/json (MIT) and the Khronos OpenXR loader (Apache-2.0); see THIRD_PARTY_NOTICES.txt.

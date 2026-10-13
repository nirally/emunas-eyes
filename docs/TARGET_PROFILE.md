# Target Profile — Mirror's Edge Catalyst VR

Every entry is marked **confirmed** (checked on this PC or by the user) or **assumed** (not yet verified).

## User and setup (from the setup wizard, 2026-10-13)

| Item | Value | Status |
|---|---|---|
| Game | Mirror's Edge Catalyst, Steam (appid 1233570), launched via EA app | confirmed (detected) |
| Install path | `E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst` (23.4 GB) | confirmed (detected) |
| Exe version | MirrorsEdgeCatalyst.exe 1.0.3.47248, Steam buildid 10351434 | confirmed (detected) |
| Auto-update | Steam AutoUpdateBehavior=0 (always update) — **must be changed before Stage 0** | confirmed (detected) |
| Other mods | None — vanilla Steam install, no loader/proxy DLLs present | confirmed (detected + user) |
| Online | Mod targets offline/single-player use only; user accepted | confirmed (user) |
| OS | Windows 11 Pro 10.0.26300 | confirmed (detected) |
| GPU / CPU / RAM | GTX 1080 Ti / i7-10700 / 32 GB | confirmed (detected) |
| Headset | Meta Quest 2, Touch controllers | confirmed (user) |
| Connection | Steam Link (wireless) → SteamVR | confirmed (user) |
| Active OpenXR runtime | SteamVR (`steamxr_win64.json`); Meta runtime also installed (`E:\Meta Horizon`) | confirmed (detected) |
| Controller family to test first | Meta Touch (Quest 2) | confirmed (user) |
| Environment mode | agentic (Claude Code) | confirmed (user) |
| Code comfort | Can follow instructions — explain once, do the technical work | confirmed (user) |
| Modding experience | None | confirmed (user) |
| Headset session length | Up to 1 hour (scripts still kept ≤15 items) | confirmed (user) |
| Game familiarity | Very experienced Catalyst player; will flag anything that feels off | confirmed (user) |

## Preferences

| Item | Value | Status |
|---|---|---|
| Gate mode | Hard gate for Stages 0–4, then revisit | confirmed (user) |
| Combat | Physical punching: bring controller to enemy with real punch motion → hit | confirmed (user) |
| Locomotion | Loves smooth locomotion; **hates snap** → smooth turn default, snap turn available but off | confirmed (user) |
| Play stance | Standing, seated supported | confirmed (user) |
| Handedness | Right-handed; left-handed fully supported | confirmed (user) |

## Game description (user-confirmed, Screen 3)

- First-person; cutscenes use cinematic, often third-person, cameras.
- Melee only (punches, kicks, momentum attacks); no player firearms.
- Parkour core: wall-run, vault, slide, climb, ledge grab, bar swing, ziplines, MAG Rope (swing/pull), quick turns, rolls. No vehicles, no swimming.
- Many cinematic cutscenes and scripted camera moments (takedowns, set pieces). No lock-on.
- UI fairly clean: Runner Vision, objective markers, health/focus, prompts; heavy map/menu UI.

## Game dossier (research 2026-10-13)

**Engine and platform**
- Frostbite 3 (2016 build). Install layout (`Data/`, `Patch/`, `Engine.BuildInfo_Win64_retail.dll`) matches Frostbite. — confirmed (on-disk evidence)
- DirectX 11 renderer (the Helix 3D Vision fix targets DX11). — assumed, verify in Stage 0
- No further patches expected; EA shut down the game's servers on 2023-12-08. — confirmed (Wikipedia)

**DRM, anti-cheat, online**
- Steam version uses **Denuvo** (anti-tamper copy protection) plus an EA account login through the EA app. — confirmed (Steam community, multiple reports)
- No multiplayer anti-cheat found. Online features (leaderboards, user-made time trials, social) are dead since the server shutdown. — confirmed
- Reports say the game can hang on a loading screen after the intro while it looks for the dead servers. Community workaround: block the game's network access. — assumed; see Q-001
- **Rules for this project:** we don't touch Denuvo, the license token or the EA login. The mod loads *alongside* the game. Prefer hooks that don't patch game code bytes (data edits through Frosty, vtable hooks, and D3D11/DXGI hooks), because patched code inside Denuvo-protected regions is fragile. — decision

**Modding landscape**
- **Frosty Tool Suite / Frosty Mod Manager:** the established data-modding tool. It supports Catalyst, including the Steam version (Mod Manager 1.0.6+ with platform launching). Mods are `.fbmod` data edits. — confirmed
- **Camera On Head (Nexus #332):** a data-only mod that moves the camera to Faith's animated head joint (`Skeleton_Faith` camera position plus `CameraForwardAxis` in `FaithCharacter`). This shows the camera rig is **data-driven and editable through Frosty**, a big help for Stage 1. Known issues: vertical angle stays locked, some camera shakes missing. — confirmed (mod page)
- **FOV:** set in game options on Steam. Uses *vertical* FOV. Older FOV-unlock mods go up to 120. — confirmed
- **Helix Mod 3D Vision fix (DX11):** stereo shader fixes for shadows, lights, reflections, sky/sun/moon, lens flares. The HUD is one shader/texture and can't be split. Requires motion blur off. This is a **map of which effects break in stereo** for Stage 2. — confirmed
- **OpenGameCamera** (MIT): open-source Frostbite free-camera with reverse-engineered SDK classes and DebugRenderer hooks. Targets SWBF2, not Catalyst; its approach transfers. — confirmed
- **BF2VR** (GPL-3.0, archived 2023): Frostbite VR mod for Battlefront II. Camera via GameRenderer/RenderView, alternate-eye rendering, ViGEm gamepad emulation, OpenXR. Shows the Frostbite camera path is reachable. Alternate-eye stereo is a low rung we won't copy. — confirmed
- **Mirror's Edge (2008) VR mod** (letsgosportsteam): a different game and engine (UE3). Useful for parkour comfort ideas only. — confirmed
- **VorpX:** commercial, fake 3D only. Not a real conversion. — confirmed
- **No existing VR mod for Catalyst.** — confirmed (search 2026-10-13)

**Systems that take over the camera** (all need Stage 1 handling)
Head bob, landing dips, rolls, wall-run tilt (camera *roll*), quick-turn 180, bar swings, ladders/pipes, ledge climbs, ziplines, MAG Rope swing/pull, slides, takedowns/finishers, death and fall camera, scripted set pieces, cinematic cutscenes, Gridnode/hack sequences, loading transitions.

**UI surface**
Runner Vision (red world highlights and ghost runner line), objective/waypoint markers, health/shield (focus), prompts, time-trial timers, damage indicators, big map and menu screens. Helix found the HUD is one shader, so per-element splitting needs a deeper hook. — assumed

**Accessibility features to preserve:** subtitles, remapping, FOV slider, HUD element toggles. — assumed

## Engine and hooks

| Item | Value | Status | How found |
|---|---|---|---|
| Exe packing | Denuvo + EA Activation64 wrapper. Static import table hidden (only Activation64 + delay-load d3d12 visible) | confirmed | dumpbin |
| Graphics API | **D3D11** (d3d11.dll + dxgi.dll loaded; no d3d12 loaded). Swapchain 1920x1080, fmt 28 (R8G8B8A8_UNORM), 3 buffers, fullscreen | confirmed | runtime module list + Present hook |
| DLL search policy | System DLLs (version, dinput8, winmm, dxgi) always load from System32. App-folder proxies of system DLLs are ignored | confirmed | version.dll proxy test, 2026-10-13 |
| Entry method | **Proxy of NvCameraSDK64.dll** (game-folder Ansel library, loaded at module #70, before d3d11 at #74). Original moved to `mecvr\NvCameraSDK64_orig.dll` and forwarded | confirmed | game loaded it: log + window title |
| NvCameraSDK64 exports | getConfiguration, getConfigurationSize, getSessionConfigurationSize, getVersion, setConfiguration, setStopSessionCallback, setUpdateCameraFunc, stopSession, updateCamera | confirmed | dumpbin |
| Ansel calls at main menu | getVersion x2, setConfiguration x1. updateCamera not called at the menu | confirmed | proxy call counters |
| Ansel calls in gameplay | unknown | assumed | PLAYTEST-0 item 4 |
| Backup door | Engine.BuildInfo_Win64_retail.dll (export getBuildInfo, loaded at #87, after d3d11) | confirmed | dumpbin |
| Frame rate (flat, menu) | ~145–200 fps uncapped at 1080p | confirmed | Present hook |
| View constant buffer | 1136-byte CB: [4..7] screen, [8..23] view (camera = -R^T t), [24..39] projection (reverse-Z infinite, near 0.06, TAA jitter) | confirmed | CB capture + poke |
| Game camera (menu) | [[exe+0x23e7f78]+0x400]+0x70 = position; LinearTransform rows before it. Writing it moves the view | confirmed (menu, 3 launches) | value hunt + poke + pointer scan, D-009 |
| RenderView copy | Transform + prev transform, FOV 0.9599 rad, near 0.05, far 10000, aspect 1.7778. No short static path | confirmed (menu) | D-009 |
| Game camera in gameplay | Menu chain does NOT hold in gameplay. Gameplay camera set via exe+0x3536f10 (SetCameraData) from the camera manager, return site exe+0x327bc8b | confirmed | RECON-1 hardware write-watch + code dump, D-010 |
| Camera data block (0x90) | +0x00 transform A (LinearTransform), +0x40 transform B, +0x80 vec4 (compared; meaning TBD); dirty flags cam+0x120 | confirmed (layout), assumed (meanings) | disassembly |
| World units | meters (near plane 0.06, camera on a rooftop at y=62.6) | confirmed | projection + scene |
| Anti-aliasing | TAA (projection jitter) — Stage 2 concern | confirmed | CB capture |

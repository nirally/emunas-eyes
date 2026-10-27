# Runner's Sight

A full 6DoF motion-controller VR mod for Mirror's Edge Catalyst (Steam, v1.0.3.47248).

**Status:** Stages 0-9 passed; Stage 10 (sweep and package) in progress. See `docs/PHASE_PLAN.md`.

- Single-player/offline only. Does not modify or bypass the game's DRM.
- Ships only its own files. No game assets or executables are redistributed.
- End-user install, controls and known issues: `dist/README.md`. Release zip: `powershell -File tools\package.ps1 -Version <x.y.z>` (after a Release build) -> `out\`.
- Developer install: `tools\deploy.ps1` / `tools\undeploy.ps1`.

Project docs live in `docs/`. Game-specific data lives in `config/manifest.json`.

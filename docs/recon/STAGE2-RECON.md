# Stage 2 recon notes

## exe+0x31a40e0 — per-frame view builder (disassembly: docs/recon/fn_31a40e0.asm)
- ebx = local view/player count = vcall [ [exe+0x?]+0x68 ] at 0x31a4212 (object at rip-0xc40f0f from 0x31a4208).
- r12 = number of camera/view objects in the list at [exe+0x2415e50] (begin +8, end +0x10).
- Outer loop r15d = 0..ebx-1 (player index). Inner loop over the list: object rdi qualifies if [rdi+0x1b8f] != 0 and [rdi+0x28] == r15d.
- Per qualifying object: reserve a 0x1120-byte RenderView in a local vector; 0x3277100(obj, rv) fills it; copy rv[0..0x90] to rv+0xfd0 (likely the previous transform for TAA/motion vectors); if 0x319c0e0(...) then SetCameraData(rv, derived) [our 2nd override site]; then per-view fields at rv+0xfb0..0xfc4 from obj+0x10/0x14/0x18/0x20/0x28.
- After the loops (0x31a4706): per-view vcall [r11+0x60]. Likely submission to the renderer.
- This is Frostbite's split-screen path. Hypothesis H1: two views (left/right eye) through this loop = native per-frame stereo.

## Next steps
1. Read ebx at the menu and in gameplay (expect 1). Find the object behind the +0x68 vcall.
2. Count list entries and qualifying objects; dump one object (layout around +0x10..+0x28, +0x1b8f).
3. Find where RenderView viewport/target is set (split-screen viewports) and how many RenderViews the renderer actually draws.
4. Experiment (menu, safe): duplicate the qualifying view for index 0 (two RenderViews, same player) and see whether the renderer draws both (frame capture: count of view constant buffers per frame).
Fallback H2: render twice per Present (synchronized sequential, game time frozen on the second pass).

## Results (2026-10-13)
- View list at the menu: 1 entry. View object: vtable exe+0x1b17d28, +0x10/+0x14 viewport x/y = 0/0, +0x1c/+0x1e = 1920/1080 (u16), +0x20 aspect 1.7778, +0x28 player index 0, +0x1b8f active.
- Local view count: vfunc exe+0x360ada0 = (end-begin)/0x2c0 of a vector at obj+0x7e0 → number of local players (704-byte entries). Not the place to add an eye.
- H1 attempts A (copy → list-unlink crash) and B (same object twice → render hang) failed. See D-017.

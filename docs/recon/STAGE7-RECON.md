# Stage 7 recon — Faith's arm skeleton (2026-10-17)

All found on the desktop with tools/drive (no headset), in the rooftop start area, looking down.

## How Faith's body reaches the GPU (confirmed: trace + poke)
- Faith's body pieces are the draws with stencil ref 0x20 (D-036), all `DrawIndexedInstanced`, static meshes in
  bind pose (immutable VBs) skinned **in the vertex shader**.
- VS t0 = a 1 MB DYNAMIC buffer the CPU rewrites every frame (Map/Unmap, two alternating copies). VS cb0 = a slot
  in a 1 MB constant-buffer ring (VSSetConstantBuffers1 with offsets, 16 constants per draw).
- Per-draw slot (cb0): uint4 (start, count, start, count) in 16-byte rows of t0 (the repeat is presumably the
  previous-frame range for motion vectors - same values seen); then lighting coefficients; a 3x4 world matrix
  (Faith's position, yaw only); more ints.
- A piece's block in t0: 9 rows of lighting (SH), then one 3x4 row-major rigid matrix per bone (rows = rotation |
  translation), **world space** (translations ~ (550, 67, 23) where Faith stood; y up; metres).
  Check: bones = (count - 9) / 3 for every piece (7, 5, 23, 11, 15, 13, 18, 81, 7, 25).
- Writing those rows again with Map(WRITE_NO_OVERWRITE) right before the piece's draw changes what is drawn
  (faith_bones poke, diagnostics.bone_poke_piece): piece-wide +30 cm lifted the torso (23 bones), the legs (18) and
  **both bare arms and hands (81 bones)**. A single bone moves only the skin weighted to it, not its children
  (skinning matrices, not a hierarchy).

## The arm skeleton from the matrices (tools/bone_solve, bone_solve_1.txt)
600 frames of the 81-bone piece (BONEREC; running, turning, idle). For a child bone c and its parent p there is a
bind-space point x with S_c(x) = S_p(x) every frame (the joint). Least squares over all frames for every pair:
- Two clear chains, one per arm: shoulder (y 67.84) -> elbow 0.282 m lower -> wrist ~0.24 m lower -> 5 finger
  chains. Upper arm 0.282 m both sides. Twist bones partway (0.083 m below the elbow; 0.359 m from the shoulder).
- Arm A (x ~ 551.1): shoulder bones 13/16 (+14, 17, 20), elbow 3/7/9 (+23 twist, 43), hand 1/4 and 2/8, fingers
  5, 6, 10-12, 25-40, 42.
- Arm B (x ~ 550.7): shoulder 41/56 (+15, 19, 57, 58), elbow 50 (+79), hand 44/53, fingers 45-49, 51, 52, 54,
  55, 60-78.
- Bones that pivot on the same point (a bone and its twist/helper) pair up both ways, so parent vs child isn't
  decided there; the IK doesn't need it (it needs the 3 joints per arm and which bones ride on each segment).
- Roots with no fit (0 = identity, 21, 59, 18/22, 24/80): their parents (clavicle/spine) aren't in this piece.
- Which arm is Faith's left: not decided yet (needs her facing from the cb0 world matrix).

## What this means for Stage 7
No game code patching and no data mod needed: the arms can be re-posed by rewriting the 81-bone piece's
matrices before its draw, S' = C_segment * S, with C the IK correction for the segment the bone rides on (upper
arm / forearm / hand, fingers with the hand). Shadow and other passes read the same rows, so they follow.
Open: index stability across sessions/LODs/outfits (identify by structure, not index), Faith's left vs right,
the controller-grip-to-hand-bone offset, and how game animations (climbs, grabs) blend with ours.

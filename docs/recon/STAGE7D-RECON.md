# Stage 7D recon — Faith's fingers (2026-10-19)

Desktop, tools/drive; offline with tools/finger_recon on bones_rec.bin (1500 frames, 2026-10-18) and a MESHDUMP of
the arm piece.

## The bone matrices alone don't give the fingers
- The game curls all her fingers together (turn angles against the palm correlate 0.7-1.0 across fingers), so motion
  can't tell which bone belongs to which finger.
- Finger bones beyond the first knuckle don't pivot on clean points (pair fits 2-15 mm, some pairs never turn
  against each other in the recording). Only the first knuckle (hand bone <-> proximal) fits well on some fingers
  (0.1-1.4 mm on her right hand; on her left the index and middle proximals never turn against the hand bone).

## The arm piece's mesh (MESHDUMP trigger, faith_bones::dump_mesh)
- One draw: 34272 indices from 0, base vertex 0, R16 indices; vertex buffer IMMUTABLE, 837328 bytes, stride 60
  (several LODs; this draw uses vertices 0-6279).
- Vertex: float3 bind-space position at 0 (y up, metres, her A-pose: wrists at x +-0.461, y 1.027, z 0.172);
  bytes 20-27 eight bone indices; 28-35 eight weights summing to 255.
- **A vertex's bone v is the piece's matrix row v + 1** (row 0 is an identity). Skinning the bind mesh with recorded
  frames: 4663 of 17691 triangle edges stretched past 50% with row v, 33 with v + 1, 5988/1759 with v -2/+2.
- She faces +z in bind space (forearms angle 17 cm forward: elbow z -0.005, wrist z +0.172; her left arm is at +x,
  "Faith's left arm is arm 1"). D-044's "facing -z" note was wrong; the code always used the computed front.

## Fingers from the skin
- Per hand 12 finger bones, 4 fingers x 3 (rows ~2 cm apart across the hand, mirrored left/right), index nearest
  her front; a 2-bone thumb (her right: 8 -> 25, left: 51 -> 61) at the front, nearer the wrist. Palm skin: her left
  44/47/49; her right's knuckle skin is nearly all on the proximals themselves (0.5 shared vs 22 on the left).
- Bones of one finger share skin at their knuckles (15-34% of the smaller bone's weight); neighbouring fingers only
  at the web (8-11%); the thumb chain is 2 long. So: chains from the tips inwards along the most-shared skin;
  chains of 3 = fingers.
- Her right hand: index 28-36-35, middle 26-27-39, ring 29-30-40, pinky 31-33-32. Left: index 63-67-75, middle
  65-71-77, ring 66-72-70, pinky 68-74-73.
- Knuckles (distance from the wrist joint, her right hand): first knuckle 5.4-6.6 cm (matrix fit where it fits,
  else shared skin), PIP 9.3-10.1 cm, DIP 10.4-12.0 cm; skin to ~12.5 cm. The bind hand is relaxed, a little curled towards the palm.

Pictures: fingers_7d_off.png (the game's relaxed hands), fingers_7d_on.png (simulated hands: her left points, her
right makes a fist; the thumb keeps the game's pose), fingers_7d_running.png (the same while running).

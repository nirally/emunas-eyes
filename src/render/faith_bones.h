#pragma once
// Stage 7: Faith's bone matrices on their way to the GPU (recon, then IK arms).
//
// Found (docs/recon/STAGE7-RECON.md): Faith's body pieces (stencil 0x20, D-036) are skinned in the vertex shader
// from a 1 MB dynamic buffer the CPU rewrites every frame (VS t0). Each piece's per-draw constants (VS cb0 slot,
// in a 1 MB constant-buffer ring) start with (start, count) in 16-byte rows; the piece's block is 9 rows of
// lighting followed by one 3x4 world-space bone matrix (3 rows) per bone.
#include <filesystem>

struct ID3D11DeviceContext;
struct ID3D11Resource;

namespace mecvr::faith_bones {

// cb_capture, after the game's Map of a big buffer: the pointer to hand back to the game. For every 1 MB
// constant ring (the game rotates through more than 16) and the bone buffers Faith's draws read, that is a
// shadow in ordinary memory - the mapped memory is write-combined and reading it back cost the frame rate (3 fps).
void* on_map(ID3D11Resource* res, void* mapped, unsigned size, bool constant_buffer);
// cb_capture, before the game's Unmap: a shadowed buffer is copied to the real mapping.
void on_unmap(ID3D11Resource* res);
// ctx_hooks, before an indexed instanced draw (inside the hook guard), with the draw's index range.
void before_draw(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex);
// Once per Present: frame boundary, 1 s log of Faith's pieces.
void on_present();

// diagnostics.bone_poke_piece (1-based, in the frame's draw order; 0 = off) / diagnostics.bone_poke_bone
// (0-based within the piece, -1 = all): those bones are lifted 30 cm, to see which part of Faith they move.
void set_poke(int piece, int bone);
// BONEREC trigger file (mod folder): the next `frames` frames of Faith's biggest piece (the arms) are appended
// to bones_rec.bin: per frame uint32 bone count, then 12 floats (3x4 world matrix) per bone. For the skeleton
// solver (tools/bone_solve).
void request_record(const std::filesystem::path& dir, int frames);
// MESHDUMP trigger file (7D recon): the next draw of the 81-bone arm piece writes its vertex buffers (every bound
// slot), index buffer and draw arguments to mesh_dump.txt / mesh_vb<slot>.bin / mesh_ib.bin (tools/finger_recon:
// each bone's skin centroid in bind space).
void request_mesh_dump(const std::filesystem::path& dir);
bool active();

// body.arms (7A): learn Faith's arms (arm_solve) from the first ~400 frames she is drawn, retried up to 5 times;
// the result is logged ([bones] "arms from ..."). The IK (7B) uses it.
void set_arms(bool on);
bool arms_known();
// False after a frame in which her body wasn't stencil-marked (0x20; ledge hangs): the hands' wall probes
// would see her own arms then.
bool body_marked();
// body.forearm_twist (7B): the forearm's share of the hand's twist about it (arm_ik::share_twist).
void set_forearm_twist(float k);
// body.elbow_follow_hand (PLAYTEST-7i): the elbow's side from the controller's angle (1) or the body rule (0).
void set_elbow_follow_hand(float k);
// body.turn_back_in_attacks (BUG-024/025): during an attack her whole body turns back to the way it faced the view
// when the attack began (the game's attacks swing it 60-160 deg away from your arms).
void set_turn_back(bool on);
// body.fingers (7D): her fingers curl with yours (index from the trigger, the others from the grip; the thumb stays
// with the hand). Found from the arm piece's mesh once her arms are known ([bones] "fingers of arm ...").
void set_fingers(bool on);
// body.arms_game_when_hanging (PLAYTEST-7c): while the game holds either of her hands above its shoulder (ledge hangs,
// pipes, ladders, wall climbs, ziplines; PLAYTEST-7e - was both hands) her arms fade to the game's animation, and back after.
void set_game_when_hanging(bool on);
// GetTickCount64() of the last frame either of the game's hands was 10+ cm above its shoulder (hangs, pipes,
// ladders, wall climbs; 0 = never). Room-scale holds its walking stick then (PLAYTEST-7d: it made her climb).
unsigned long long hand_up_at();

}  // namespace mecvr::faith_bones

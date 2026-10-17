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
// ctx_hooks, before an indexed instanced draw (inside the hook guard).
void before_draw(ID3D11DeviceContext* ctx);
// Once per Present: frame boundary, 1 s log of Faith's pieces.
void on_present();

// diagnostics.bone_poke_piece (1-based, in the frame's draw order; 0 = off) / diagnostics.bone_poke_bone
// (0-based within the piece, -1 = all): those bones are lifted 30 cm, to see which part of Faith they move.
void set_poke(int piece, int bone);
// BONEREC trigger file (mod folder): the next `frames` frames of Faith's biggest piece (the arms) are appended
// to bones_rec.bin: per frame uint32 bone count, then 12 floats (3x4 world matrix) per bone. For the skeleton
// solver (tools/bone_solve).
void request_record(const std::filesystem::path& dir, int frames);
bool active();

// body.arms (7A): learn Faith's arms (arm_solve) from the first ~400 frames she is drawn, retried up to 5 times;
// the result is logged ([bones] "arms from ..."). The IK (7B) uses it.
void set_arms(bool on);
bool arms_known();

}  // namespace mecvr::faith_bones

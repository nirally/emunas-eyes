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

// cb_capture, at Unmap: a constant buffer write (the 1 MB rings are copied while this module is active).
void on_cb_write(ID3D11Resource* res, const void* data, unsigned size);
// cb_capture, at Unmap: a write to a big dynamic non-constant buffer (the bone data; copied while recording).
void on_bone_write(ID3D11Resource* res, const void* data, unsigned size);
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

}  // namespace mecvr::faith_bones

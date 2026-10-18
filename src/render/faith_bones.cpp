// Faith's bone matrices (Stage 7). See faith_bones.h.
#include "render/faith_bones.h"

#include <d3d11_1.h>
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/arm_ik.h"
#include "core/arm_solve.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"

namespace mecvr::faith_bones {
namespace {

constexpr unsigned kRingBytes = 1048576;
constexpr unsigned kHeaderRows = 9;  // lighting rows before a piece's bones

std::atomic<int> g_poke_piece{0}, g_poke_bone{-1};
std::atomic<int> g_record_left{0};

// 7A: learning Faith's arms from the game's own motion (arm_solve), once per session.
constexpr int kLearnFrames = 400, kLearnTries = 5;
std::atomic<bool> g_arms_on{true}, g_learning{false};
arm_solve::Learner g_learner;
arm_solve::Result g_arms;  // ok once learned
int g_tries = 0;
// 7B: IK onto the controllers, once the arms are known. The arm piece = the piece with the learned row count.
std::atomic<bool> g_ik_ready{false};
unsigned g_arm_count = 0;
int g_left = -1;               // which solved arm is Faith's left (-1: still voting)
bool g_hand_ok[2] = {};        // the hand turns with the controller (its bind-space frame is known)
vm::Quat g_hand_bind[2];
std::atomic<float> g_forearm_twist{0.5f};
float g_twist_sum = 0;  // 5 s log: mean |twist| the hand had about the forearm, degrees       // the hand's axes in bind space, as the glove's hand space (arm_ik::hand_frame)
float g_vote = 0;
int g_votes = 0;
int g_patched_other = 0;  // 5 s log: rows of other pieces moved with the arms
int g_ik_frames = 0, g_ik_arms = 0, g_ik_clamped = 0, g_ik_far = 0;  // 5 s log
int g_not_arms = 0;     // 5 s log: arm blocks rejected (the IK then waits for the arm piece's own draw)
int g_far_eye[3] = {};  // too-far hands by the rendered eye (mono/unknown, left, right)
float g_far_min = 1e9f, g_far_max = 0, g_near_max = 0;  // shoulder -> target (m)
double g_copy_ms = 0, g_copy_max_ms = 0;  // shadow -> real copies at Unmap
unsigned g_copies = 0;
bool learning() { return g_learning.load(std::memory_order_relaxed); }

double now_ms() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}
std::filesystem::path g_dir;
FILE* g_rec = nullptr;
int g_recorded = 0;
std::mutex g_mutex;
// Shadows of every 1 MB dynamic buffer (Faith's bone buffers and per-draw constant rings among them; the game
// rotates through hundreds): the game writes into the shadow, Unmap copies the pages it wrote (write watch) to
// `real`; the shadow keeps the last contents for us to read. The rings are mapped ~330 times a frame (once per
// draw), so whole-buffer copies cost 11 ms a frame; dirty pages ~0.15 ms.
struct Shadow {
    unsigned char* mem = nullptr;
    size_t bytes = 0;
    void* real = nullptr;
    unsigned frame = 0;  // g_frame when the game last wrote it: older contents are stale
    const unsigned char* data() const { return mem; }
    unsigned char* data() { return mem; }
    size_t size() const { return bytes; }
};
std::unordered_map<ID3D11Resource*, Shadow> g_shadow;
constexpr size_t kPage = 4096;
unsigned g_frame = 1;
const Shadow* contents(ID3D11Resource* r) {  // written this frame, else null
    auto it = g_shadow.find(r);
    return it == g_shadow.end() || !it->second.mem || it->second.frame != g_frame ? nullptr : &it->second;
}

struct Piece { ID3D11Resource* buf; unsigned start, count; unsigned long long vb; };
std::vector<Piece> g_pieces;      // this frame's Faith pieces, in draw order
std::vector<Piece> g_last_pieces;  // the previous frame's (log)

int piece_index(ID3D11Resource* buf, unsigned start) {
    for (size_t i = 0; i < g_pieces.size(); ++i)
        if (g_pieces[i].buf == buf && g_pieces[i].start == start) return int(i);
    return -1;
}

// 7B: Faith's arms onto the controllers. The game's pose (this frame's shadow of the arm piece) -> two-bone IK
// per arm (arm_ik) -> every bone riding on a segment corrected. The corrected matrices are kept as (game matrix ->
// new matrix) pairs: every Faith piece carries its own copies of the skeleton's matrices (a wrist wrap is its own
// piece, drawn before the arms: live, a gap at her right wrist), so each piece's rows that equal an arm bone's are
// replaced, right before the piece's first draw this frame (later draws of it - shadow passes - read the same
// rows). The shadows keep the game's own pose. Hands: the grip pose's wrist point (the glove's wrist band).
struct Fix { float game[12], now[12]; };
std::vector<Fix> g_fixes;  // this frame's (render thread)
unsigned g_fix_frame = 0;
// Where the arm piece was last frame, in rows after the frame's first Faith piece: the bone buffers rotate and
// her pieces move about in them, but stay together in the same order (live: always 212 rows after the first).
unsigned g_first_start = 0;  // this frame's first Faith piece
int g_arm_rel = -1;

// Fills g_fixes from the arm block at `off` in buf's shadow; false if it isn't the arms (layout changed) or no
// hand is tracked. Under g_mutex.
bool compute(ID3D11Resource* buf, size_t off, int bones) {
    g_fixes.clear();
    const camera_override::Hands h = camera_override::rendered_hands();
    if (!h.valid[0] && !h.valid[1]) return false;
    const Shadow* v = contents(buf);
    if (!v || off + size_t(bones) * 48 > v->size() || int(g_arms.side.size()) != bones) return false;
    static std::vector<float> rows;  // render thread only
    rows.assign(reinterpret_cast<const float*>(v->data() + off), reinterpret_cast<const float*>(v->data() + off) + bones * 12);
    {
        const auto* b = reinterpret_cast<const arm_solve::Bone*>(rows.data());
        vm::Vec3 s[2], e[2], w[2];
        for (int a = 0; a < 2; ++a) {
            const auto& arm = g_arms.arm[a];
            s[a] = b[arm.shoulder_bone].apply(arm.shoulder);
            e[a] = b[arm.elbow_bone].apply(arm.elbow);
            w[a] = b[arm.wrist_bone].apply(arm.wrist);
            // The arms really are here: the hand bone meets the wrist where the arm's bone does, and the shoulders
            // are at the head (live: blocks that weren't the arms gave shoulders 600 m away).
            if (arm.hand_bone >= 0 && (b[arm.hand_bone].apply(arm.wrist) - w[a]).length() > 0.01f) { ++g_not_arms; return false; }
            if ((s[a] - h.view_pos).length() > 1.5f) { ++g_not_arms; return false; }
        }
        if (g_left < 0) {  // the camera's right (Faith's view) over 60 frames: the arm on the left is her left
            if (h.view_right.length() < 0.5f) return false;
            g_vote += (s[0] - s[1]).dot(h.view_right) > 0 ? 1.f : -1.f;
            if (++g_votes >= 60) {
                g_left = g_vote > 0 ? 1 : 0;
                LOG_I("bones", "Faith's left arm is arm %d (%d of %d frames agree)", g_left, int(std::fabs(g_vote) + g_votes) / 2, g_votes);
                // Her front in bind space (y up): up x (left shoulder -> right shoulder).
                const vm::Vec3 front =
                    vm::Vec3{0, 1, 0}.cross(g_arms.arm[1 - g_left].shoulder - g_arms.arm[g_left].shoulder).normalized();
                for (int a = 0; a < 2; ++a) {
                    const auto& arm = g_arms.arm[a];
                    g_hand_ok[a] = arm.hand_bone >= 0 && arm_ik::hand_frame(arm.wrist, arm.hand_joints, front, g_hand_bind[a]);
                    const vm::Vec3 f = g_hand_bind[a].rotate({0, 0, -1}), t = g_hand_bind[a].rotate({0, 1, 0});
                    LOG_I("bones", "  arm %d hand (bind): bone %d, %zu joints, fingers %+.2f %+.2f %+.2f, thumb side %+.2f %+.2f %+.2f%s", a,
                          arm.hand_bone, arm.hand_joints.size(), f.x, f.y, f.z, t.x, t.y, t.z,
                          g_hand_ok[a] ? "" : " - not found, the hand keeps the game's turn");
                }
            }
            return false;
        }
        ++g_ik_frames;
        bool moved = false;
        for (int hand = 0; hand < 2; ++hand) {
            if (!h.valid[hand]) continue;
            const int a = hand == 0 ? g_left : 1 - g_left;
            const vm::Quat grip = (h.rot[hand] * vm::Quat::axis_angle({1, 0, 0}, -1.5707963f)).normalized();
            const vm::Vec3 target = h.pos[hand] + grip.rotate({hand == 0 ? -0.042f : 0.042f, -0.004f, 0.108f});
            const float dist = (target - s[a]).length();
            if (dist > 1.0f) {  // not where an arm can be (tracking lost, cutscene camera)
                ++g_ik_far;
                ++g_far_eye[head_pose::rendered_eye() + 1 > 2 ? 0 : head_pose::rendered_eye() + 1];
                g_far_min = dist < g_far_min ? dist : g_far_min;
                g_far_max = dist > g_far_max ? dist : g_far_max;
                continue;
            }
            g_near_max = dist > g_near_max ? dist : g_near_max;
            vm::Vec3 out = s[a] - s[1 - a];
            out.y = 0;
            const vm::Vec3 pole = (vm::Vec3{0, -1, 0} + out.normalized() * 0.7f).normalized();  // elbows down and out
            arm_ik::Result r = arm_ik::solve(s[a], e[a], w[a], target, pole);
            if (g_hand_ok[a]) {  // the hand takes the controller's turn (the glove's hand space)
                const float* m = &rows[g_arms.arm[a].hand_bone * 12];
                vm::LinearTransform lt{};  // basis = the matrix's columns
                for (int c = 0; c < 3; ++c) {
                    lt.right[c] = m[c * 4];
                    lt.up[c] = m[c * 4 + 1];
                    lt.back[c] = m[c * 4 + 2];
                }
                r.hand = arm_ik::hand_to((vm::quat_of(lt) * g_hand_bind[a]).normalized(), grip, w[a], r.wrist);
                g_twist_sum += std::fabs(arm_ik::twist_angle(r.fore.q.conjugate() * r.hand.q, (w[a] - e[a]).normalized())) * 57.29578f;
                arm_ik::share_twist(r, e[a], w[a], g_forearm_twist.load());
            }
            g_ik_clamped += r.clamped;
            ++g_ik_arms;
            for (int k = 0; k < bones; ++k) {
                if (g_arms.side[k] != a) continue;
                const int seg = g_arms.segment[k];
                Fix f;
                memcpy(f.game, &rows[k * 12], sizeof(f.game));
                memcpy(f.now, f.game, sizeof(f.now));
                arm_ik::apply(seg == arm_solve::kUpper ? r.upper : seg == arm_solve::kFore ? r.fore : r.hand, f.now);
                g_fixes.push_back(f);
            }
            moved = true;
        }
        if (!moved) return false;
    }
    return true;
}

// Rows of a piece's block (bones at `off` in buf's shadow) equal to a fixed arm bone's game matrix get the new
// one, in the real buffer. Returns how many. Takes g_mutex itself (our Map must be outside it).
int patch(ID3D11DeviceContext* ctx, ID3D11Resource* buf, size_t off, int bones) {
    static std::vector<std::pair<int, const Fix*>> hits;  // render thread only
    hits.clear();
    {
        std::lock_guard lock(g_mutex);
        const Shadow* v = contents(buf);
        if (g_fixes.empty() || !v || off + size_t(bones) * 48 > v->size()) return 0;
        const float* rows = reinterpret_cast<const float*>(v->data() + off);
        for (int k = 0; k < bones; ++k)
            for (const Fix& f : g_fixes) {
                int j = 0;
                while (j < 12 && std::fabs(rows[k * 12 + j] - f.game[j]) <= 1e-5f * (1.f + std::fabs(f.game[j]))) ++j;
                if (j == 12) { hits.push_back({k, &f}); break; }
            }
    }
    if (hits.empty()) return 0;
    // Our own Map is nested in the draw hook (not shadowed): straight into the real buffer.
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m)) && m.pData) {
        float* rows = reinterpret_cast<float*>(static_cast<unsigned char*>(m.pData) + off);
        for (const auto& hit : hits) memcpy(rows + hit.first * 12, hit.second->now, 48);  // fixes unchanged until next frame
        ctx->Unmap(buf, 0);
    }
    return int(hits.size());
}

}  // namespace

void set_forearm_twist(float k) { g_forearm_twist = k; }

void set_poke(int piece, int bone) {
    g_poke_piece = piece;
    g_poke_bone = bone;
}
bool active() {
    return g_poke_piece.load() != 0 || g_record_left.load() > 0 || learning() || g_ik_ready.load(std::memory_order_relaxed);
}

void set_arms(bool on) {
    std::lock_guard lock(g_mutex);
    g_arms_on = on;
    // Learn (again) when switched on and not learned yet.
    g_learning = on && !g_arms.ok && g_tries < kLearnTries;
    g_ik_ready = on && g_arms.ok;
}
bool arms_known() {
    std::lock_guard lock(g_mutex);
    return g_arms.ok;
}

void request_record(const std::filesystem::path& dir, int frames) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
    g_recorded = 0;
    g_record_left = frames;
}

void* on_map(ID3D11Resource* res, void* mapped, unsigned size, bool constant_buffer) {
    if (!active() || size != kRingBytes || !mapped) return mapped;
    std::lock_guard lock(g_mutex);
    (void)constant_buffer;
    Shadow& s = g_shadow[res];
    if (!s.mem) {  // first time: start from what the buffer holds (one slow read)
        s.mem = static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
        if (!s.mem) {
            g_shadow.erase(res);
            return mapped;
        }
        s.bytes = size;
        memcpy(s.mem, mapped, size);
        ResetWriteWatch(s.mem, size);
    }
    s.real = mapped;
    s.frame = g_frame;
    return s.mem;
}

void on_unmap(ID3D11Resource* res) {
    std::lock_guard lock(g_mutex);
    auto it = g_shadow.find(res);
    if (it == g_shadow.end() || !it->second.real) return;
    Shadow& s = it->second;
    void* pages[kRingBytes / kPage];
    ULONG_PTR n = kRingBytes / kPage;
    ULONG gran = 0;
    const double t0 = now_ms();
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, s.mem, s.bytes, pages, &n, &gran) == 0) {
        for (ULONG_PTR i = 0; i < n; ++i) {
            const size_t off = static_cast<unsigned char*>(pages[i]) - s.mem;
            memcpy(static_cast<unsigned char*>(s.real) + off, s.mem + off, gran);
        }
    } else {
        memcpy(s.real, s.mem, s.bytes);
    }
    const double ms = now_ms() - t0;
    g_copy_ms += ms;
    if (ms > g_copy_max_ms) g_copy_max_ms = ms;
    ++g_copies;
    s.real = nullptr;
}

void before_draw(ID3D11DeviceContext* ctx) {
    if (!active()) return;
    ID3D11DepthStencilState* dss = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&dss, &ref);
    if (!dss) return;
    D3D11_DEPTH_STENCIL_DESC dd;
    dss->GetDesc(&dd);
    dss->Release();
    if (!dd.StencilEnable || ref != 0x20) return;  // Faith's body (D-036)

    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&c1)))) return;
    ID3D11Buffer* cb = nullptr;
    UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(0, 1, &cb, &first, &num);
    c1->Release();
    if (!cb) return;
    unsigned start = 0, count = 0;
    {
        std::lock_guard lock(g_mutex);

        const auto* v = contents(cb);
        if (v && (first + 1) * 16 <= v->size()) {
            const unsigned* u = reinterpret_cast<const unsigned*>(v->data() + first * 16);
            start = u[0];
            count = u[1];
        }
    }
    cb->Release();
    if (count <= kHeaderRows || (count - kHeaderRows) % 3) return;

    ID3D11ShaderResourceView* srv = nullptr;
    ctx->VSGetShaderResources(0, 1, &srv);
    if (!srv) return;
    ID3D11Resource* buf = nullptr;
    srv->GetResource(&buf);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd;
    srv->GetDesc(&vd);
    srv->Release();
    if (!buf) return;
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    unsigned long long vb_id = vb ? reinterpret_cast<unsigned long long>(vb) ^ offset : 0;
    if (vb) vb->Release();

    int idx;
    {
        std::lock_guard lock(g_mutex);

        idx = piece_index(buf, start);
        if (idx >= 0) {  // already handled this frame (the same piece drawn again: shadows, other passes)
            buf->Release();
            return;
        }
        g_pieces.push_back({buf, start, count, vb_id});
        idx = int(g_pieces.size()) - 1;
    }
    if (g_ik_ready.load()) {
        const size_t fe = vd.Buffer.FirstElement;
        bool have;
        {
            std::lock_guard lock(g_mutex);
            // The first Faith piece this frame: the IK from the arm block where it was last frame (all her pieces
            // share one bone buffer); at the arm piece itself again if that failed.
            const bool arm = count == g_arm_count;
            if (g_fix_frame != g_frame) {
                g_fix_frame = g_frame;
                g_first_start = start;
                if (arm || g_arm_rel >= 0)
                    compute(buf, (fe + (arm ? start : start + g_arm_rel) + kHeaderRows) * 16, int(g_arm_count - kHeaderRows) / 3);
                else
                    g_fixes.clear();
            } else if (g_fixes.empty() && arm) {
                compute(buf, (fe + start + kHeaderRows) * 16, int(g_arm_count - kHeaderRows) / 3);
            }
            if (arm) g_arm_rel = int(start - g_first_start);
            have = !g_fixes.empty();
        }
        if (have) {
            const int n = patch(ctx, buf, (fe + start + kHeaderRows) * 16, int(count - kHeaderRows) / 3);
            if (count != g_arm_count) g_patched_other += n;
        }
    }
    if (idx + 1 == g_poke_piece.load()) {
        // Our own Map is nested in the draw hook, so it isn't shadowed: write the real buffer and the shadow.
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m)) && m.pData) {
            const size_t off = (size_t(vd.Buffer.FirstElement) + start + kHeaderRows) * 16;
            const int bones = int(count - kHeaderRows) / 3, only = g_poke_bone.load();
            std::lock_guard lock(g_mutex);
            auto sh = g_shadow.find(buf);
            float* shadow = sh != g_shadow.end() && sh->second.size() >= off + bones * 48
                                ? reinterpret_cast<float*>(sh->second.data() + off)
                                : nullptr;
            float* rows = reinterpret_cast<float*>(static_cast<unsigned char*>(m.pData) + off);
            for (int b = 0; b < bones; ++b)
                if (only < 0 || only == b) {  // row 1 (y) translation: up 30 cm
                    if (shadow) shadow[b * 12 + 4 + 3] += 0.3f;
                    rows[b * 12 + 4 + 3] = shadow ? shadow[b * 12 + 4 + 3] : rows[b * 12 + 4 + 3] + 0.3f;
                }
            ctx->Unmap(buf, 0);
        }
    }
    buf->Release();
}

void on_present() {
    if (!active()) return;
    static ULONGLONG last = 0;
    std::lock_guard lock(g_mutex);
    if (g_record_left.load() > 0 && !g_pieces.empty()) {
        // The biggest piece (the arms, 81 bones, STAGE7-RECON) from the bone buffer shadow.
        const Piece* big = &g_pieces[0];
        for (const Piece& p : g_pieces)
            if (p.count > big->count) big = &p;
        const auto* v = contents(big->buf);
        const size_t from = (size_t(big->start) + kHeaderRows) * 16;
        const unsigned bones = (big->count - kHeaderRows) / 3;
        if (v && from + bones * 48 <= v->size()) {
            if (!g_rec) _wfopen_s(&g_rec, (g_dir / L"bones_rec.bin").c_str(), L"wb");
            if (g_rec) {
                fwrite(&bones, 4, 1, g_rec);
                fwrite(v->data() + from, 48, bones, g_rec);
                ++g_recorded;
            }
        }
        if (--g_record_left <= 0 && g_rec) {
            fclose(g_rec);
            g_rec = nullptr;
            LOG_I("bones", "bones_rec.bin written: %d frames of the %u-bone piece", g_recorded, bones);
        }
    }
    static ULONGLONG learn_log = 0;
    if (learning() && GetTickCount64() - learn_log >= 5000) {
        learn_log = GetTickCount64();
        unsigned big = 0;
        for (const Piece& p : g_pieces) big = p.count > big ? p.count : big;
        LOG_I("bones", "learning Faith's arms: %d of %d frames; %zu pieces this frame (biggest %u rows), "
              "%zu shadowed; %u shadow copies in 5 s, %.3f ms avg, %.3f max",
              g_learner.frames(), kLearnFrames, g_pieces.size(), big, g_shadow.size(), g_copies,
              g_copies ? g_copy_ms / g_copies : 0.0, g_copy_max_ms);
        g_copy_ms = g_copy_max_ms = 0;
        g_copies = 0;
    }
    if (learning() && !g_pieces.empty()) {
        // 7A: the arms are the biggest piece (81 bones, STAGE7-RECON); feed its matrices to the learner.
        const Piece* big = &g_pieces[0];
        for (const Piece& p : g_pieces)
            if (p.count > big->count) big = &p;
        const auto* v = contents(big->buf);
        const size_t from = (size_t(big->start) + kHeaderRows) * 16;
        const int bones = int(big->count - kHeaderRows) / 3;
        if (v && bones >= 20 && from + size_t(bones) * 48 <= v->size()) {
            if (g_learner.bones() != bones) g_learner.reset(bones);
            g_learner.add(reinterpret_cast<const arm_solve::Bone*>(v->data() + from));
            if (g_learner.frames() >= kLearnFrames) {
                const double t0 = now_ms();
                g_arms = g_learner.solve();
                ++g_tries;
                LOG_I("bones", "arms from %d frames of the %d-bone piece (try %d, %.0f ms; %d joints, %d wrist candidates, %d arm chains): %s",
                      g_learner.frames(), bones, g_tries, now_ms() - t0, g_arms.joints, g_arms.wrists, g_arms.chains,
                      g_arms.why);
                if (!g_arms.ok)
                    for (const auto& c : g_arms.found)
                        LOG_I("bones", "  chain found (bind space): shoulder %.3f %.3f %.3f, elbow %.3f %.3f %.3f, wrist %.3f %.3f %.3f",
                              c.shoulder.x, c.shoulder.y, c.shoulder.z, c.elbow.x, c.elbow.y, c.elbow.z, c.wrist.x, c.wrist.y,
                              c.wrist.z);
                if (g_arms.ok)
                    for (int a = 0; a < 2; ++a) {
                        const auto& arm = g_arms.arm[a];
                        const arm_solve::Bone* b = reinterpret_cast<const arm_solve::Bone*>(v->data() + from);
                        const vm::Vec3 s = b[arm.shoulder_bone].apply(arm.shoulder), e = b[arm.elbow_bone].apply(arm.elbow),
                                       w = b[arm.wrist_bone].apply(arm.wrist);
                        int n[3] = {0, 0, 0};
                        for (int k = 0; k < bones; ++k)
                            if (g_arms.side[k] == a) ++n[g_arms.segment[k]];
                        LOG_I("bones", "  arm %d: upper arm %.3f m, forearm %.3f m; bones upper %d, forearm %d, hand %d", a,
                              (s - e).length(), (e - w).length(), n[0], n[1], n[2]);
                    }
                g_learning = !g_arms.ok && g_tries < kLearnTries;
                g_arm_count = big->count;
                g_ik_ready = g_arms.ok && g_arms_on.load();
                g_learner.reset(bones);
                g_copy_ms = g_copy_max_ms = 0;
                g_copies = 0;

            }
        }
    }
    static ULONGLONG ik_log = 0;
    if (g_ik_ready.load() && GetTickCount64() - ik_log >= 5000) {
        ik_log = GetTickCount64();
        LOG_I("bones", "IK arms in 5 s: %d frames, %d arms posed (%d out of reach, clamped), %d hands too far; hand twist about "
              "the forearm %.0f deg avg, forearm takes %.2f; %d rows of other pieces followed", g_ik_frames, g_ik_arms, g_ik_clamped, g_ik_far,
              g_ik_arms ? g_twist_sum / g_ik_arms : 0.f, g_forearm_twist.load(), g_patched_other);
        if (g_ik_far)
            LOG_I("bones", "  hands too far: %.2f-%.2f m from the shoulder (mono %d, left eye %d, right eye %d)", g_far_min,
                  g_far_max, g_far_eye[0], g_far_eye[1], g_far_eye[2]);
        if (g_ik_arms) LOG_I("bones", "  posed hands up to %.2f m from the shoulder", g_near_max);
        if (g_not_arms) LOG_I("bones", "  %d blocks looked for the arms weren't them", g_not_arms);
        g_not_arms = 0;
        g_far_eye[0] = g_far_eye[1] = g_far_eye[2] = 0;
        g_far_min = 1e9f;
        g_far_max = g_near_max = 0;
        g_patched_other = 0;
        g_ik_frames = g_ik_arms = g_ik_clamped = g_ik_far = 0;
        g_twist_sum = 0;
    }
    if (g_poke_piece.load() && GetTickCount64() - last >= 1000 && !g_pieces.empty()) {
        last = GetTickCount64();
        char line[512];
        size_t len = 0;
        for (size_t i = 0; i < g_pieces.size() && len < sizeof(line) - 48; ++i)
            len += snprintf(line + len, sizeof(line) - len, " %zu:%u+%u(%u bones, mesh %llx)", i + 1, g_pieces[i].start,
                            g_pieces[i].count, (g_pieces[i].count - kHeaderRows) / 3, g_pieces[i].vb & 0xffffff);
        LOG_I("bones", "Faith pieces this frame:%s; poking piece %d bone %d", line, g_poke_piece.load(),
              g_poke_bone.load());
    }
    ++g_frame;
    g_last_pieces.swap(g_pieces);
    g_pieces.clear();
}

}  // namespace mecvr::faith_bones

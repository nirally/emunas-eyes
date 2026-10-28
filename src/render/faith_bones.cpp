// Faith's bone matrices (Stage 7). See faith_bones.h.
#include "render/faith_bones.h"

#include <d3d11_1.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/arm_ik.h"
#include "core/arm_solve.h"
#include "core/finger_rig.h"
#include "core/hand_pose.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"
#include "render/hand_render.h"
#include "render/screenshot.h"

namespace mecvr::faith_bones {
namespace {

constexpr unsigned kRingBytes = 1048576;
constexpr unsigned kHeaderRows = 9;  // lighting rows before a piece's bones

std::atomic<int> g_poke_piece{0}, g_poke_bone{-1};
std::atomic<int> g_record_left{0};
std::atomic<bool> g_mesh_dump{false};  // MESHDUMP: the next arm piece draw's mesh to files

// 7A: learning Faith's arms from the game's own motion (arm_solve), once per session.
constexpr int kLearnFrames = 400, kLearnTries = 1000;  // retried while her arms barely move (each try ~4 s)
// After the IK starts the learner keeps the arm piece's frames (marked ones only) and solves each kRefineEvery
// frames on their own, kRefineWindows times (~10 min), for bones the first solve couldn't place
// (arm_solve::adopt_new_bones). Windows as short as the first solve: the motion test grows with the frames, so long
// still stretches hid joints (desktop: 12000 frames found no arms, 1500-frame windows mostly failed).
constexpr int kRefineEvery = kLearnFrames, kRefineWindows = 150;
int g_refines = 0, g_refine_same = 0, g_refine_other = 0, g_refine_failed = 0;  // 10-window log
std::vector<int> g_pending;  // per bone: side * 4 + segment a window placed it on (-1 none); taken when the next agrees
std::atomic<bool> g_arms_on{true}, g_learning{false};
arm_solve::Learner g_learner;
arm_solve::Result g_arms;  // ok once learned
int g_tries = 0;
// 7B: IK onto the controllers, once the arms are known. The arm piece = the piece with the learned row count.
std::atomic<bool> g_ik_ready{false};
// Her body isn't always stencil-marked (0x20): in a ledge hang the game draws everything with ref 0x02, and in the
// headset whole frames went unmarked in normal play too (PLAYTEST-7b: 18-144 per 5 s; the right glove, drawn
// before the arms, then kept the game's pose - "the palm glitching"). So once the IK runs every skinned draw of
// 3+ bones is taken (patch() only replaces rows equal to an arm bone's game matrix: others are untouched), and
// the IK runs at the earliest draw that can find the arm block - from her first marked piece (g_arm_rel), else
// from a piece whose offset to the arm block is known: each piece size learns it (rows, same buffer) whenever
// the arms are found at their own draw, marked or not; compute() checks every guess.
bool g_marked_now = false;               // a marked piece this frame (render thread)
std::atomic<bool> g_unmarked{false};     // the last frame had none (walls and touch off, body_marked())
int g_unmarked_frames = 0;               // 5 s log
// Under g_mutex.
struct Seen { ID3D11Resource* buf; size_t row; unsigned count; };
std::vector<Seen> g_seen_now;                       // this frame's skinned draws before the IK found the arms
std::unordered_map<unsigned, long long> g_rel_by_count;  // piece size -> arm block row minus the piece's row
int g_tries_now = 0;
int g_early = 0;  // 5 s log: frames the IK ran before the arm piece's own draw
unsigned g_arm_count = 0;
// PLAYTEST-8n (arm_solve::merge_pieces): when her biggest marked piece is too small for both arms (her jacket: 51
// bones; the arm piece without it: 81), the arms are learned from all her marked pieces of a frame as one skeleton.
// The layout - piece sizes in draw order, each piece's rows after the first's (one bone buffer), which rows are copies -
// is fixed over kLayoutFrames frames alike; g_arms' bones are then the merged ones, read from the layout wherever one
// of its pieces is drawn. Patching is unchanged (every piece's rows equal to an arm bone's game matrix).
// Her outfit can change mid-session: kRelearnFrames frames of her marked pieces without the arms -> learned again.
constexpr int kOnePieceBones = 64, kLayoutFrames = 30, kRelearnFrames = 2700;  // ~30 s
struct Layout {
    std::vector<unsigned> counts;  // rows per piece (header included), in draw order
    std::vector<long long> rel;    // each piece's first row after the first piece's
    arm_solve::Merge merge;
    int hand_piece = -1;  // the piece with most hand bones (its mesh gives the fingers)
};
Layout g_layout, g_layout_next;
bool g_multi = false;  // g_arms / g_learner are over g_layout's merged bones
std::vector<std::vector<arm_solve::Bone>> g_layout_frames;  // g_layout_next's frames so far (each: its pieces' bones in a row)
int g_gone_run = 0;  // frames in a row with her marked pieces and the hands tracked, but no arm block found (IK on)
// BUG-035 (PLAYTEST-9a): after aborting the first mission her jacket was gone but g_gone_run never filled (no hands
// tracked at frame end in most frames, ~20 s of free roam). So also: kOutfitFrames marked frames in a row where no
// marked piece has a size the arms were learned on -> learned again.
constexpr int kOutfitFrames = 180;  // ~2 s
int g_outfit_run = 0;
unsigned g_arms_at = 0;  // the last frame compute() found the arm block
int g_left = -1;              // which solved arm is Faith's left (-1: still voting)
bool g_hand_ok[2] = {};        // the hand turns with the controller (its bind-space frame is known)
vm::Quat g_hand_bind[2];
std::atomic<float> g_forearm_twist{0.5f};
std::atomic<float> g_elbow_follow_hand{0.f};  // PLAYTEST-7j: 1 looked "worse"
// 7D fingers (finger_rig, docs/recon/STAGE7D-RECON.md): built once from the arm piece's mesh, read back at its draw
// once her arms and her left are known; per arm. The fits are each bone's joint with the hand bone (first solve).
std::atomic<bool> g_fingers_on{true};
int g_finger_state = 0;  // 0 not yet, 1 built, -1 couldn't (logged)
finger_rig::Hand g_rig[2];
finger_rig::Fits g_fits[2];
vm::Vec3 g_front;                     // her front in bind space (set with g_left)
std::vector<signed char> g_finger_of;  // per bone: finger * 3 + joint (index 0-2 ... pinky 9-11), -1 none
float g_curl_sum[2] = {};              // 5 s log: index, others
int g_curl_n = 0;
// Hanging (PLAYTEST-7c: "I would like the arms to be on the ledge position rather than following the controllers"):
// either of the game's wrists 10+ cm above its shoulder for kHangFrames frames in a row (~0.35 s: a jump raises
// both hands for ~0.15 s - desktop; PLAYTEST-7e: was both hands, which pipes and wall climbs rarely hold) -> the IK fades out over kFadeFrames frames, and back in once they've been down
// for kLetGoFrames. The stencil mark can't tell (normal play in the headset has unmarked frames too).
std::atomic<bool> g_game_when_hanging{true};
// kLetGoFrames 30 (was 6, PLAYTEST-7d: "the arms glitch"): a hand comes down for a moment between pipe steps.
// kGapFrames: frames with no hand up that don't break a climb's run (PLAYTEST-7e).
constexpr int kHangFrames = 30, kLetGoFrames = 30, kFadeFrames = 18, kGapFrames = 8;
int g_up_run = 0, g_down_run = 0;
std::atomic<ULONGLONG> g_hand_up_at{0};  // last frame either of her game hands was above its shoulder
int g_hand_up_frames = 0;                // 5 s log
bool g_hanging = false;
float g_ik_weight = 1.f;
int g_hang_frames = 0;  // 5 s log
arm_ik::UnmarkedHold g_jacket_hold;  // BUG-039: her jacket's arms keep the game's animation while she isn't marked
bool g_jacket_held = false;
int g_jacket_held_frames = 0;  // 5 s log
float g_twist_prev[2] = {};      // last posed frame's twist per arm (arm_ik::share_twist), for continuity
unsigned g_twist_at[2] = {};
int g_twist_kept = 0, g_twist_held = 0;            // 5 s log: frames a twist past 180 deg stayed on its side (would have flipped)
float g_twist_sum = 0;  // 5 s log: mean |twist| the hand had about the forearm, degrees       // the hand's axes in bind space, as the glove's hand space (arm_ik::hand_frame)
float g_vote = 0;
int g_votes = 0;
int g_patched_other = 0;  // 5 s log: rows of other pieces moved with the arms
int g_ik_frames = 0, g_ik_arms = 0, g_ik_clamped = 0, g_ik_far = 0;  // 5 s log
int g_not_arms = 0;     // 5 s log: arm blocks rejected (the IK then waits for the arm piece's own draw)
int g_no_hands = 0, g_stale = 0, g_no_arm_piece = 0;  // 5 s log: frames the IK couldn't run, and why
unsigned g_seen_counts[4] = {}, g_seen_n = 0;
int g_no_faith = 0;  // 5 s log: frames with no Faith piece at all
  // 5 s log: piece sizes seen in frames without the arm piece
int g_far_eye[3] = {};  // too-far hands by the rendered eye (mono/unknown, left, right)
float g_far_min = 1e9f, g_far_max = 0, g_near_max = 0;  // shoulder -> target (m)
// 5 s log (PLAYTEST-7i: "sometimes when punching it punches with the other hand"): her shoulders' turn from the
// view (the game's attacks twist her torso), and posed elbows inside her chest (a box below and between the shoulders).
float g_torso_turn_max = 0;
int g_torso_turned = 0, g_elbow_in_chest = 0;
// BUG-024/025 (PLAYTEST-7j: "still does not look right"): the game's attacks swing her whole body 60-160 deg
// (desktop recording: legs, torso and arms alike), so her shoulders leave your arms. From the attack button until
// kTurnBackMs after the last one, her whole body turns back about the vertical line through your head to the turn
// from the view it had when the attack began, then eases out (body.turn_back_in_attacks).
constexpr unsigned long long kTurnBackMs = 1200;
constexpr int kTurnFadeFrames = 36;
std::atomic<bool> g_turn_back{true};
arm_ik::Rigid g_body;    // this frame's turn (render thread)
bool g_body_on = false;  // g_body is applied to every bone of her this frame
float g_turn0 = 0, g_turn_prev = 0, g_turn_w = 0;
bool g_in_attack = false;
float g_body_max = 0;  // 5 s log
int g_body_frames = 0, g_turned_rows = 0;
// 5 s log (PLAYTEST-7k, air punches only: "less"): her chest's front after the turn-back vs your hands' direction
// (head -> midpoint of the controllers, level; both hands out 15+ cm), so head-looks don't count. [0] attacks, [1] not.
float g_off_hands_max[2] = {};
// 5 s log (PLAYTEST-8e: on a train, "faith body and hands glitched without stopping"): her shoulders' middle against
// the view from one posed frame to the next (a jump = her body and the camera out of step), and posed <-> game pose flips.
vm::Vec3 g_rel_prev;
// PLAYTEST-8i/8j (after the train cutscene: "the hands are moving on their own towards the headset while
// flickering"; the 5 s figures: up to 481 of 654 arms clamped out of reach, the view at her head): when her arms come
// back to the IK after 0.5 s without it, 4 s of her shoulders and hands in the view's frame are logged, and a second
// with most arms clamped takes a headset screenshot (3 a session, 20 s apart).
unsigned g_last_ik_frame = 0, g_trace_until = 0;
int g_sec_arms = 0, g_sec_clamped = 0, g_auto_shots = 0;
ULONGLONG g_sec_start = 0, g_last_auto_shot = 0;
unsigned g_rel_frame = 0;
float g_rel_jump_max = 0;
int g_rel_jumps = 0, g_pose_flips = 0;
bool g_posed_prev = false;
int g_off_hands_n[2] = {}, g_off_hands_30[2] = {};
// body.reach_scale (PLAYTEST-7l: punches went 0.65 m from her shoulder, her arm reaches 0.53 m, up to a third of
// arms clamped short): your reach is learned as you play and your hand's distance is pulled in past kReachKnee of
// her reach, so your full reach is hers (arm_ik::reach_remap).
constexpr float kReachKnee = 0.6f, kMaxExtraReach = 0.25f;
std::atomic<bool> g_reach_scale{false};  // PLAYTEST-7m: on, the elbows bent; the user liked 7l (off) more
float g_reach_hist[2][3] = {};
unsigned g_reach_i[2] = {};
float g_your_reach = 0, g_her_reach = 0, g_pulled_max = 0;  // render thread; g_pulled_max for the 5 s log
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
FILE* g_rec_pieces = nullptr;  // pieces_rec.bin: per frame uint32 pieces, then per marked piece uint32 bones + matrices
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

struct Piece { ID3D11Resource* buf; unsigned start, count; unsigned long long vb; bool marked; bool patched = false; };
int g_late_patched = 0;  // 5 s log: pieces patched at a later draw (their first came before the solve)
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
struct Fix { float game[12], now[12]; bool arm = true; };  // arm = false: only her body's turn (g_body)
bool is_identity(const float* m) {
    static const float kI[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (int i = 0; i < 12; ++i)
        if (std::fabs(m[i] - kI[i]) > 1e-6f) return false;
    return true;
}
std::vector<Fix> g_fixes;  // this frame's (render thread)
unsigned g_fix_frame = 0;
// Where the arm piece was last frame, in rows after the frame's first Faith piece: the bone buffers rotate and
// her pieces move about in them, but stay together in the same order (live: always 212 rows after the first).
unsigned g_first_start = 0;  // this frame's first Faith piece
int g_arm_rel = -1;

// Fills g_fixes from the arm block at `off` in buf's shadow; false if it isn't the arms (layout changed) or no
// hand is tracked. Under g_mutex.
bool compute_rows(const float* src, int bones);
bool compute(ID3D11Resource* buf, size_t off, int bones) {
    g_fixes.clear();
    const Shadow* v = contents(buf);
    if (!v || off + size_t(bones) * 48 > v->size()) { ++g_stale; return false; }
    return compute_rows(reinterpret_cast<const float*>(v->data() + off), bones);
}

// g_layout's merged bones from the pieces whose first piece's bones (header included) start at row `base` of buf's
// shadow. False if a piece is past the shadow.
bool gather(ID3D11Resource* buf, long long base, std::vector<arm_solve::Bone>& out) {
    const Shadow* v = contents(buf);
    if (!v || base < 0) return false;
    const Layout& L = g_layout;
    for (size_t p = 0; p < L.counts.size(); ++p)
        if (size_t(base + L.rel[p] + L.counts[p]) * 16 > v->size()) return false;
    out.resize(L.merge.src.size());
    for (size_t u = 0; u < out.size(); ++u) {
        const auto [p, b] = L.merge.src[u];
        memcpy(&out[u], v->data() + size_t(base + L.rel[p] + kHeaderRows + 3 * b) * 16, 48);
    }
    return true;
}

// This frame's marked pieces as g_layout's merged bones. A frame laid out otherwise starts a new layout (`rebuild`),
// taken after kLayoutFrames frames alike. Under g_mutex.
bool layout_frame(std::vector<arm_solve::Bone>& out, bool rebuild) {
    std::vector<const Piece*> m;
    for (const Piece& p : g_pieces)
        if (p.marked) m.push_back(&p);
    if (m.size() < 2) return false;
    std::vector<unsigned> counts;
    std::vector<long long> rel;
    for (const Piece* p : m) {
        if (p->buf != m[0]->buf) return false;
        counts.push_back(p->count);
        rel.push_back((long long)p->start - (long long)m[0]->start);
    }
    if (counts == g_layout.counts && rel == g_layout.rel) return gather(m[0]->buf, m[0]->start, out);
    if (!rebuild) return false;
    if (counts != g_layout_next.counts || rel != g_layout_next.rel) {
        g_layout_next = {counts, rel, {}, -1};
        g_layout_frames.clear();
    }
    const Shadow* v = contents(m[0]->buf);
    if (!v) return false;
    std::vector<arm_solve::Bone> fr;
    for (const Piece* p : m) {
        const size_t from = (size_t(p->start) + kHeaderRows) * 16, n = (p->count - kHeaderRows) / 3;
        if (from + n * 48 > v->size()) return false;
        const auto* b = reinterpret_cast<const arm_solve::Bone*>(v->data() + from);
        fr.insert(fr.end(), b, b + n);
    }
    g_layout_frames.push_back(std::move(fr));
    if (int(g_layout_frames.size()) < kLayoutFrames) return false;
    std::vector<int> bones;
    for (unsigned c : counts) bones.push_back(int(c - kHeaderRows) / 3);
    std::vector<std::vector<const arm_solve::Bone*>> frames;
    for (const auto& f : g_layout_frames) {
        frames.emplace_back();
        size_t at = 0;
        for (int n : bones) {
            frames.back().push_back(f.data() + at);
            at += size_t(n);
        }
    }
    g_layout_next.merge = arm_solve::merge_pieces(frames, bones);
    g_layout_frames.clear();
    char line[160] = "";
    size_t len = 0;
    for (size_t p = 0; p < counts.size() && len < sizeof(line) - 24; ++p)
        len += snprintf(line + len, sizeof(line) - len, " %d bones +%lld rows", bones[p], rel[p]);
    if (g_layout_next.merge.src.size() > 255) {  // finger_rig keeps bone numbers in bytes
        LOG_W("bones", "her pieces this frame:%s - %zu bones in all, too many", line, g_layout_next.merge.src.size());
        return false;
    }
    g_layout = g_layout_next;
    g_learner.reset(0);  // its frames were of another layout
    LOG_I("bones", "her arm piece is too small for both arms: learning them from her %zu pieces (%s), %zu bones without copies",
          counts.size(), line + 1, g_layout.merge.src.size());
    return false;
}

bool compute_rows(const float* src, int bones) {
    g_fixes.clear();
    const camera_override::Hands h = camera_override::rendered_hands();
    if (!h.valid[0] && !h.valid[1]) { ++g_no_hands; return false; }
    if (int(g_arms.side.size()) != bones) { ++g_stale; return false; }
    static std::vector<float> rows;  // render thread only
    rows.assign(src, src + bones * 12);
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
        g_arms_at = g_frame;
        if (g_left < 0) {  // the camera's right (Faith's view) over 60 frames: the arm on the left is her left
            if (h.view_right.length() < 0.5f) return false;
            g_vote += (s[0] - s[1]).dot(h.view_right) > 0 ? 1.f : -1.f;
            if (++g_votes >= 60) {
                g_left = g_vote > 0 ? 1 : 0;
                LOG_I("bones", "Faith's left arm is arm %d (%d of %d frames agree)", g_left, int(std::fabs(g_vote) + g_votes) / 2, g_votes);
                // Her front in bind space (y up): up x (left shoulder -> right shoulder).
                const vm::Vec3 front =
                    vm::Vec3{0, 1, 0}.cross(g_arms.arm[1 - g_left].shoulder - g_arms.arm[g_left].shoulder).normalized();
                g_front = front;
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
        if (g_last_ik_frame + 45 < g_frame) {
            g_trace_until = g_frame + 360;
            LOG_I("bones", "arms back to yours after %u frames without: tracing 4 s (x right, y up, z ahead of the view, m)", g_frame - g_last_ik_frame);
        }
        g_last_ik_frame = g_frame;
        vm::Vec3 chest_right = s[1 - g_left] - s[g_left], view_right = h.view_right;
        chest_right.y = view_right.y = 0;
        chest_right = chest_right.normalized();
        vm::Vec3 chest_mid = (s[0] + s[1]) * 0.5f, chest_front = vm::Vec3{0, 1, 0}.cross(chest_right);
        {
            const vm::Vec3 rel = chest_mid - h.view_pos;
            if (g_rel_frame + 1 == g_frame) {
                const float jump = (rel - g_rel_prev).length();
                g_rel_jump_max = std::fmax(g_rel_jump_max, jump);
                g_rel_jumps += jump > 0.05f;
            }
            if (g_rel_frame != g_frame) g_rel_prev = rel;
            g_rel_frame = g_frame;
        }
        if (view_right.length() > 0.5f) {
            const float turn = std::fabs(std::atan2(view_right.normalized().cross(chest_right).y, view_right.normalized().dot(chest_right))) * 57.29578f;
            g_torso_turn_max = turn > g_torso_turn_max ? turn : g_torso_turn_max;
            g_torso_turned += turn > 20.f;
        }
        g_body_on = false;
        if (view_right.length() > 0.5f) {
            const float turn = arm_ik::yaw_between(view_right, chest_right);
            if (hand_pose::simulated() && ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000))
                camera_override::note_attack();  // desktop tests: the mouse attacks
            // Punches only: a kick swings her legs round with the body (up to 156 deg in PLAYTEST-8b) and turned back
            // about your head it glitched every time - kicks keep the game's turn (a kick in a combo fades it out).
            const unsigned long long now_ms64 = GetTickCount64();
            const bool attack = g_turn_back.load() && now_ms64 - camera_override::last_attack() < kTurnBackMs &&
                                now_ms64 - camera_override::last_kick() >= kTurnBackMs;
            if (attack && !g_in_attack && g_turn_w <= 0.f) g_turn0 = g_turn_prev;  // a new fight move: its starting turn
            g_in_attack = attack;
            g_turn_prev = turn;
            g_turn_w = attack ? 1.f : std::fmax(0.f, g_turn_w - 1.f / kTurnFadeFrames);
            if (g_turn_w > 0.f) {
                const float d = g_turn0 - turn, a = std::atan2(std::sin(d), std::cos(d)) * g_turn_w;
                g_body_on = std::fabs(a) > 0.002f;
                if (g_body_on) {
                    g_body = arm_ik::yaw_about(h.view_pos, a);
                    for (int k = 0; k < 2; ++k) {
                        s[k] = g_body.apply(s[k]);
                        e[k] = g_body.apply(e[k]);
                        w[k] = g_body.apply(w[k]);
                    }
                    chest_mid = g_body.apply(chest_mid);  // the chest box of the log turns with her
                    chest_right = g_body.q.rotate(chest_right);
                    chest_front = g_body.q.rotate(chest_front);
                    g_body_max = std::fmax(g_body_max, std::fabs(a) * 57.29578f);
                    ++g_body_frames;
                }
            }
        }
        if (h.valid[0] && h.valid[1]) {
            vm::Vec3 to_hands = (h.pos[0] + h.pos[1]) * 0.5f - h.view_pos;
            to_hands.y = 0;
            if (to_hands.length() > 0.15f) {
                const float deg = std::acos(std::fmax(-1.f, std::fmin(1.f, to_hands.normalized().dot(chest_front)))) * 57.29578f;
                const int k = g_turn_w > 0.f ? 0 : 1;
                g_off_hands_max[k] = std::fmax(g_off_hands_max[k], deg);
                ++g_off_hands_n[k];
                g_off_hands_30[k] += deg > 30.f;
            }
        }
        // Hanging or climbing: either of the game's hands above her shoulder (a pipe or ladder takes them in turn).
        // PLAYTEST-7e ("arms glitch, less frequently"): both-hands-up only held in ledge hangs; on pipes and wall
        // climbs one hand was up 93-165 frames per 5 s with no hang, and the IK stretched her arms to the controllers.
        const bool up0 = w[0].y - s[0].y > 0.10f, up1 = w[1].y - s[1].y > 0.10f, up = up0 || up1;
        // Room-scale's walking stick would climb her.
        if (up) {
            g_hand_up_at = GetTickCount64();
            ++g_hand_up_frames;
        }
        g_down_run = up ? 0 : g_down_run + 1;
        g_up_run = up ? g_up_run + 1 : g_down_run < kGapFrames ? g_up_run : 0;  // short dips between climb steps
        if (!g_hanging && g_up_run >= kHangFrames && g_game_when_hanging.load()) {
            g_hanging = true;
            LOG_I("bones", "hanging/climbing (a game hand above the shoulder): arms go to the game's animation");
        } else if (g_hanging && (g_down_run >= kLetGoFrames || !g_game_when_hanging.load())) {
            g_hanging = false;
            LOG_I("bones", "not hanging any more: arms follow yours again");
        }
        g_hang_frames += g_hanging;
        const float fade_to = g_hanging || g_jacket_held ? 0.f : 1.f, step = 1.f / kFadeFrames;
        g_ik_weight = g_ik_weight < fade_to ? std::fmin(fade_to, g_ik_weight + step) : std::fmax(fade_to, g_ik_weight - step);
        if (g_ik_weight <= 0.f) return false;  // fully the game's: nothing to change
        bool moved = false, posed[2] = {false, false};
        for (int hand = 0; hand < 2; ++hand) {
            if (!h.valid[hand]) continue;
            const int a = hand == 0 ? g_left : 1 - g_left;
            const vm::Quat grip = (h.rot[hand] * vm::Quat::axis_angle({1, 0, 0}, -1.5707963f)).normalized();
            vm::Vec3 target = h.pos[hand] + grip.rotate({hand == 0 ? -0.042f : 0.042f, -0.004f, 0.108f}) +
                                   hand_render::wall_offset(hand);  // stops at walls like the gloves
            const float dist = (target - s[a]).length();
            if (dist > 1.0f) {  // not where an arm can be (tracking lost, cutscene camera)
                ++g_ik_far;
                ++g_far_eye[head_pose::rendered_eye() + 1 > 2 ? 0 : head_pose::rendered_eye() + 1];
                g_far_min = dist < g_far_min ? dist : g_far_min;
                g_far_max = dist > g_far_max ? dist : g_far_max;
                continue;
            }
            g_near_max = dist > g_near_max ? dist : g_near_max;
            // Your reach: the farthest a hand held for 3 frames in a row (one-frame tracking jumps don't count).
            g_reach_hist[hand][g_reach_i[hand]++ % 3] = dist;
            const float held = std::fmin(g_reach_hist[hand][0], std::fmin(g_reach_hist[hand][1], g_reach_hist[hand][2]));
            const float hers = (e[a] - s[a]).length() + (w[a] - e[a]).length() - 0.004f;
            g_your_reach = std::fmax(g_your_reach, std::fmin(held, hers + kMaxExtraReach));
            if (g_reach_scale.load()) {
                const float d2 = arm_ik::reach_remap(dist, kReachKnee * hers, hers, g_your_reach);
                if (d2 < dist) {
                    target = s[a] + (target - s[a]) * (d2 / dist);
                    g_pulled_max = std::fmax(g_pulled_max, dist - d2);
                }
            }
            g_her_reach = hers;
            const vm::Vec3 pole = arm_ik::elbow_from_hand(s[a], target, grip.rotate({0, 0, -1}), (w[a] - e[a]).length(),
                                                          arm_ik::elbow_pole(s[a], s[1 - a], target), g_elbow_follow_hand.load());
            arm_ik::Result r = arm_ik::solve(s[a], e[a], w[a], target, pole);
            if (g_frame <= g_trace_until && g_frame % 12 == 0) {
                vm::Vec3 vr = h.view_right;
                vr.y = 0;
                vr = vr.length() > 0.1f ? vr.normalized() : vm::Vec3{1, 0, 0};
                const vm::Vec3 vf = vm::Vec3{0, 1, 0}.cross(vr);
                auto in_view = [&](vm::Vec3 p) { const vm::Vec3 d = p - h.view_pos; return vm::Vec3{d.dot(vr), d.y, d.dot(vf)}; };
                const vm::Vec3 sv = in_view(s[a]), tv = in_view(target), wv = in_view(r.wrist), gv = in_view(w[a]);
                LOG_I("bones", "  trace %s hand: shoulder %+.2f %+.2f %+.2f, your hand %+.2f %+.2f %+.2f (%.2f m away), posed %+.2f %+.2f %+.2f, "
                      "game's %+.2f %+.2f %+.2f%s", hand == 0 ? "left" : "right", sv.x, sv.y, sv.z, tv.x, tv.y, tv.z, dist, wv.x, wv.y, wv.z,
                      gv.x, gv.y, gv.z, r.clamped ? " CLAMPED" : "");
            }
            {
                const ULONGLONG now = GetTickCount64();
                if (now - g_sec_start >= 1000) {
                    if (g_sec_arms > 60 && g_sec_clamped * 10 > g_sec_arms * 6 && g_auto_shots < 3 && now - g_last_auto_shot > 20000) {
                        ++g_auto_shots;
                        g_last_auto_shot = now;
                        screenshot::request();
                        LOG_W("bones", "%d of %d arms clamped out of reach in 1 s: headset screenshot %d of 3", g_sec_clamped, g_sec_arms, g_auto_shots);
                    }
                    g_sec_start = now;
                    g_sec_arms = g_sec_clamped = 0;
                }
                ++g_sec_arms;
                g_sec_clamped += r.clamped;
            }
            if (g_hand_ok[a]) {  // the hand takes the controller's turn (the glove's hand space)
                const float* m = &rows[g_arms.arm[a].hand_bone * 12];
                vm::LinearTransform lt{};  // basis = the matrix's columns
                for (int c = 0; c < 3; ++c) {
                    lt.right[c] = m[c * 4];
                    lt.up[c] = m[c * 4 + 1];
                    lt.back[c] = m[c * 4 + 2];
                }
                vm::Quat now = vm::quat_of(lt) * g_hand_bind[a];
                if (g_body_on) now = g_body.q * now;
                r.hand = arm_ik::hand_to(now.normalized(), grip, w[a], r.wrist);
                g_twist_sum += std::fabs(arm_ik::twist_angle(r.fore.q.conjugate() * r.hand.q, (w[a] - e[a]).normalized())) * 57.29578f;
                if (g_twist_at[a] + 1 < g_frame) g_twist_prev[a] = 0.f;  // not posed last frame: start fresh
                const float before = g_twist_prev[a];
                const float raw = arm_ik::twist_angle(r.fore.q.conjugate() * r.hand.q, (w[a] - e[a]).normalized());
                const bool twist_held = arm_ik::share_twist(r, e[a], w[a], g_forearm_twist.load(), &g_twist_prev[a]);
                g_twist_held += twist_held;
                g_twist_kept += !twist_held && std::fabs(g_twist_prev[a] - raw) > 1.f && std::fabs(raw - before) > 3.14159265f;
                g_twist_at[a] = g_frame;
            }
            g_ik_clamped += r.clamped;
            const vm::Vec3 el = r.elbow - chest_mid;
            g_elbow_in_chest += std::fabs(el.dot(chest_right)) < 0.16f && el.dot(chest_front) > -0.10f && el.dot(chest_front) < 0.15f &&
                                el.y < -0.05f && el.y > -0.45f;
            ++g_ik_arms;
            // 7D: her fingers from yours - each finger bone = her (corrected) hand bone * the finger's pose from the
            // bind hand, instead of riding with the hand in the game's finger pose.
            const bool fingers = g_finger_state == 1 && g_fingers_on.load() && g_rig[a].ok && int(g_finger_of.size()) == bones &&
                                 g_arms.arm[a].hand_bone >= 0;  // PLAYTEST-10a (BUG-038): else posed from the row before the bones
            arm_ik::Rigid pose[4][3];
            float hand_now[12];
            if (fingers) {
                const hand_pose::Fingers fc = hand_pose::fingers(hand);
                for (int fi = 0; fi < 4; ++fi) finger_rig::pose(g_rig[a].finger[fi], fi == 0 ? fc.index : fc.others, {}, pose[fi]);
                memcpy(hand_now, &rows[g_arms.arm[a].hand_bone * 12], sizeof(hand_now));
                if (g_body_on) arm_ik::apply(g_body, hand_now);
                arm_ik::apply(r.hand, hand_now);
                g_curl_sum[0] += fc.index;
                g_curl_sum[1] += fc.others;
                ++g_curl_n;
            }
            for (int k = 0; k < bones; ++k) {
                if (g_arms.side[k] != a) continue;
                const int seg = g_arms.segment[k];
                Fix f;
                memcpy(f.game, &rows[k * 12], sizeof(f.game));
                memcpy(f.now, f.game, sizeof(f.now));
                if (g_body_on) arm_ik::apply(g_body, f.now);
                if (fingers && g_finger_of[k] >= 0)
                    finger_rig::compose(hand_now, pose[g_finger_of[k] / 3][g_finger_of[k] % 3], f.now);
                else
                    arm_ik::apply(seg == arm_solve::kUpper ? r.upper : seg == arm_solve::kFore ? r.fore : r.hand, f.now);
                arm_ik::blend(f.game, f.now, s[a], g_ik_weight);
                g_fixes.push_back(f);
            }
            moved = posed[a] = true;
        }
        if (g_body_on) {  // the rest of the piece (and an arm not posed) only turns with her body
            for (int k = 0; k < bones; ++k) {
                if (g_arms.side[k] >= 0 && posed[g_arms.side[k]]) continue;
                Fix f;
                f.arm = false;
                memcpy(f.game, &rows[k * 12], sizeof(f.game));
                if (is_identity(f.game)) continue;  // every skinned draw's first row (other people's too)
                memcpy(f.now, f.game, sizeof(f.now));
                arm_ik::apply(g_body, f.now);
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
int g_carried = 0;  // 5 s log: rows that rode with a nearby arm bone (BUG-020)

int patch(ID3D11DeviceContext* ctx, ID3D11Resource* buf, size_t off, int bones) {
    static std::vector<std::pair<int, const Fix*>> hits;  // render thread only
    static std::vector<std::pair<int, std::array<float, 12>>> carried;  // and her body's own turned rows (last)
    static std::vector<Fix> turned;
    hits.clear();
    carried.clear();
    turned.clear();
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
        // A piece that is mostly copies of arm bones (her glove, wrist wrap) can have bones of its own riding with
        // them (her right glove: a helper 6 mm from her hand bone, rigid with it within 3 mm over 1500 frames -
        // left in the game's pose it pulled the palm): such a row within 3 cm of a copied bone moves with it. Torso
        // and leg pieces copy fewer than half their rows from the arms and are left alone. Not when her arms are learned
        // over all her pieces (D-059): every row is then a solved bone (rigid helpers ride with their partner there),
        // and in her jacket 13 rows of the hem came within 3 cm of her hands only in some frames (1-80 cm, desktop
        // recording) - carried then, left the next: PLAYTEST-8o "the legs is gliching like crazy".
        int arm_hits = 0;
        for (const auto& h : hits) arm_hits += h.second->arm;
        if (!g_multi && arm_hits && arm_hits * 2 >= bones) {
            for (int k = 0; k < bones; ++k) {
                bool hit = false;
                for (const auto& h : hits) hit |= h.first == k;
                if (hit) continue;
                const float* row = rows + k * 12;
                const Fix* best = nullptr;
                float best_d2 = 0.03f * 0.03f;
                for (const auto& h : hits) {
                    if (!h.second->arm) continue;
                    const float dx = row[3] - h.second->game[3], dy = row[7] - h.second->game[7], dz = row[11] - h.second->game[11];
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best_d2) { best_d2 = d2; best = h.second; }
                }
                if (!best) continue;
                std::array<float, 12> out;
                arm_ik::carry(best->game, best->now, row, out.data());
                carried.push_back({k, out});
            }
        }
        // Her body turning back (BUG-024/025): a piece sharing a bone with her arm piece (or with a piece of hers
        // drawn earlier this frame: her shoes only share the legs') is hers, and its own rows turn with her too.
        // Those rows join the fixes for the pieces after it.
        if (g_body_on && !hits.empty()) {
            for (int k = 0; k < bones; ++k) {
                bool done = false;
                for (const auto& h : hits) done |= h.first == k;
                for (const auto& c : carried) done |= c.first == k;
                const float* row = rows + k * 12;
                if (done || is_identity(row)) continue;
                Fix f;
                f.arm = false;
                memcpy(f.game, row, sizeof(f.game));
                memcpy(f.now, row, sizeof(f.now));
                arm_ik::apply(g_body, f.now);
                turned.push_back(f);
                carried.push_back({k, {}});
                memcpy(carried.back().second.data(), f.now, 48);
            }
        }
    }
    if (hits.empty()) return 0;
    // Our own Map is nested in the draw hook (not shadowed): straight into the real buffer.
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m)) && m.pData) {
        float* rows = reinterpret_cast<float*>(static_cast<unsigned char*>(m.pData) + off);
        for (const auto& hit : hits) memcpy(rows + hit.first * 12, hit.second->now, 48);  // fixes unchanged until next frame
        for (const auto& c : carried) memcpy(rows + c.first * 12, c.second.data(), 48);
        g_carried += int(carried.size() - turned.size());
        ctx->Unmap(buf, 0);
    }
    if (!turned.empty()) {  // after the writes: `hits` points into g_fixes
        std::lock_guard lock(g_mutex);
        g_turned_rows += int(turned.size());
        g_fixes.insert(g_fixes.end(), turned.begin(), turned.end());
    }
    return int(hits.size());
}

}  // namespace

void set_forearm_twist(float k) { g_forearm_twist = k; }
void set_elbow_follow_hand(float k) { g_elbow_follow_hand = k; }
void set_reach_scale(bool on) { g_reach_scale = on; }
void set_turn_back(bool on) { g_turn_back = on; }
void set_fingers(bool on) { g_fingers_on = on; }
void set_game_when_hanging(bool on) { g_game_when_hanging = on; }
unsigned long long hand_up_at() { return g_hand_up_at.load(); }

void set_poke(int piece, int bone) {
    g_poke_piece = piece;
    g_poke_bone = bone;
}
bool active() {
    return g_poke_piece.load() != 0 || g_record_left.load() > 0 || g_mesh_dump.load() || learning() || g_ik_ready.load(std::memory_order_relaxed);
}

void set_arms(bool on) {
    std::lock_guard lock(g_mutex);
    g_arms_on = on;
    // Learn (again) when switched on and not learned yet.
    g_learning = on && !g_arms.ok && g_tries < kLearnTries;
    g_ik_ready = on && g_arms.ok;
}
bool body_marked() { return !g_unmarked.load(std::memory_order_relaxed); }

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

void request_mesh_dump(const std::filesystem::path& dir) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
    g_mesh_dump = true;
}

namespace {
// A GPU buffer's contents through a staging copy (stalls; a one-off diagnostic). Empty on failure.
std::vector<unsigned char> read_back(ID3D11DeviceContext* ctx, ID3D11Buffer* b) {
    std::vector<unsigned char> out;
    D3D11_BUFFER_DESC d;
    b->GetDesc(&d);
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return out;
    D3D11_BUFFER_DESC sd{};
    sd.ByteWidth = d.ByteWidth;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* st = nullptr;
    if (SUCCEEDED(dev->CreateBuffer(&sd, nullptr, &st)) && st) {
        ctx->CopyResource(st, b);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)) && m.pData) {
            out.assign(static_cast<unsigned char*>(m.pData), static_cast<unsigned char*>(m.pData) + d.ByteWidth);
            ctx->Unmap(st, 0);
        }
        st->Release();
    }
    dev->Release();
    return out;
}

void dump_mesh(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex, unsigned bones) {
    const std::filesystem::path dir = g_dir;
    FILE* txt = nullptr;
    if (_wfopen_s(&txt, (dir / L"mesh_dump.txt").c_str(), L"w") || !txt) return;
    fprintf(txt, "bones %u\nindex_count %u\nstart_index %u\nbase_vertex %d\n", bones, index_count, start_index, base_vertex);
    ID3D11Buffer* vbs[8] = {};
    UINT strides[8] = {}, offsets[8] = {};
    ctx->IAGetVertexBuffers(0, 8, vbs, strides, offsets);
    for (int s = 0; s < 8; ++s) {
        if (!vbs[s]) continue;
        const auto data = read_back(ctx, vbs[s]);
        fprintf(txt, "vb %d stride %u offset %u bytes %zu\n", s, strides[s], offsets[s], data.size());
        FILE* f = nullptr;
        if (!_wfopen_s(&f, (dir / (L"mesh_vb" + std::to_wstring(s) + L".bin")).c_str(), L"wb") && f) {
            fwrite(data.data(), 1, data.size(), f);
            fclose(f);
        }
        vbs[s]->Release();
    }
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT fmt{};
    UINT ib_off = 0;
    ctx->IAGetIndexBuffer(&ib, &fmt, &ib_off);
    if (ib) {
        const auto data = read_back(ctx, ib);
        fprintf(txt, "ib format %d offset %u bytes %zu\n", int(fmt), ib_off, data.size());
        FILE* f = nullptr;
        if (!_wfopen_s(&f, (dir / L"mesh_ib.bin").c_str(), L"wb") && f) {
            fwrite(data.data(), 1, data.size(), f);
            fclose(f);
        }
        ib->Release();
    }
    D3D11_PRIMITIVE_TOPOLOGY topo{};
    ctx->IAGetPrimitiveTopology(&topo);
    fprintf(txt, "topology %d\n", int(topo));
    fclose(txt);
    LOG_I("bones", "mesh dump: arm piece (%u bones), %u indices from %u, base vertex %d -> mesh_dump.txt", bones, index_count, start_index, base_vertex);
}

// 7D: her fingers from the arm piece's mesh (finger_rig), at its draw, once. The read-back stalls once (~1 MB).
// Our Map isn't under g_mutex (cb_capture's hook takes it).
void build_fingers(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex, int bones) {
    g_finger_state = -1;  // one try
    const double t0 = now_ms();
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT fmt{};
    UINT ib_off = 0;
    ctx->IAGetIndexBuffer(&ib, &fmt, &ib_off);
    std::vector<unsigned char> vdata, idata;
    if (vb && ib) {
        vdata = read_back(ctx, vb);
        idata = read_back(ctx, ib);
    }
    if (vb) vb->Release();
    if (ib) ib->Release();
    // The draw's vertices: the range its indices span.
    const unsigned isz = fmt == DXGI_FORMAT_R16_UINT ? 2 : fmt == DXGI_FORMAT_R32_UINT ? 4 : 0;
    unsigned lo = ~0u, hi = 0;
    for (unsigned i = 0; isz && i < index_count; ++i) {
        const size_t at = ib_off + size_t(start_index + i) * isz;
        if (at + isz > idata.size()) break;
        unsigned v = 0;
        memcpy(&v, &idata[at], isz);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    std::vector<finger_rig::Vertex> verts;
    const long long first = (long long)base_vertex + lo;
    const bool parsed = lo <= hi && first >= 0 && offset < vdata.size() &&
                        finger_rig::parse(vdata.data() + offset, vdata.size() - offset, stride, unsigned(first), hi - lo + 1, bones, verts);
    std::lock_guard lock(g_mutex);
    if (!parsed) {
        LOG_W("bones", "fingers: the arm mesh isn't laid out as known (stride %u, %zu bytes, index format %d, vertices %u-%u): her "
              "fingers keep the game's pose", stride, vdata.size(), int(fmt), lo, hi);
        return;
    }
    if (g_multi) {  // the hand piece's bones -> the merged skeleton's
        const auto& to = g_layout.merge.of[g_layout.hand_piece];
        for (auto& x : verts)
            for (auto& b : x.bone) b = b < to.size() ? uint8_t(to[b]) : 0;
        bones = int(g_layout.merge.src.size());
    }
    g_finger_of.assign(bones, -1);
    int built = 0;
    static const char* names[4] = {"index", "middle", "ring", "pinky"};
    for (int a = 0; a < 2; ++a) {
        g_rig[a] = finger_rig::build(verts, g_arms, a, g_front, &g_fits[a]);
        LOG_I("bones", "fingers of arm %d (her %s hand) from %zu vertices: %s", a, a == g_left ? "left" : "right", verts.size(), g_rig[a].why);
        if (!g_rig[a].ok) continue;
        ++built;
        const vm::Vec3 w = g_arms.arm[a].wrist;
        for (int f = 0; f < 4; ++f) {
            const auto& fg = g_rig[a].finger[f];
            for (int j = 0; j < 3; ++j) g_finger_of[fg.bone[j]] = static_cast<signed char>(f * 3 + j);
            LOG_I("bones", "  %-6s bones %d %d %d, knuckles from the wrist %.3f %.3f %.3f m", names[f], fg.bone[0], fg.bone[1],
                  fg.bone[2], (fg.pivot[0] - w).length(), (fg.pivot[1] - w).length(), (fg.pivot[2] - w).length());
        }
    }
    g_finger_state = built ? 1 : -1;
    LOG_I("bones", "fingers: %d of 2 hands (%.1f ms)", built, now_ms() - t0);
}
}  // namespace

const unsigned char* shadow_of(ID3D11Resource* res, size_t* size) {
    std::lock_guard lock(g_mutex);
    const Shadow* s = contents(res);
    if (!s) return nullptr;
    *size = s->size();
    return s->data();
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

void before_draw(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex) {
    if (!active()) return;
    ID3D11DepthStencilState* dss = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&dss, &ref);
    if (!dss) return;
    D3D11_DEPTH_STENCIL_DESC dd;
    dss->GetDesc(&dd);
    dss->Release();
    const bool marked = dd.StencilEnable && ref == 0x20;  // Faith's body (D-036)
    if (!marked && !g_ik_ready.load(std::memory_order_relaxed)) return;

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
    if (!marked && count < kHeaderRows + 9) return;  // props drawn with one or two bones come by the hundred
    if (marked && count == kHeaderRows + 81 * 3 && g_mesh_dump.exchange(false))
        dump_mesh(ctx, index_count, start_index, base_vertex, (count - kHeaderRows) / 3);
    if (marked && g_finger_state == 0 && g_fingers_on.load() && g_ik_ready.load() && g_left >= 0 &&
        (g_multi ? g_layout.hand_piece >= 0 && count == g_layout.counts[g_layout.hand_piece] : count == g_arm_count))
        build_fingers(ctx, index_count, start_index, base_vertex, int(count - kHeaderRows) / 3);

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
        // Already handled this frame (the same piece drawn again: shadows, other passes) - unless its first draw
        // came before her arms were solved (desktop, mid-attack: her glove and wrist wrap floated off in the
        // game's pose while the rest of her had turned): patch it now, for its later draws.
        if (idx >= 0 && (g_pieces[idx].patched || g_fixes.empty() || g_fix_frame != g_frame)) {
            buf->Release();
            return;
        }
        if (idx < 0) {
            g_pieces.push_back({buf, start, count, vb_id, marked});
            g_marked_now |= marked;
            idx = int(g_pieces.size()) - 1;
        } else {
            ++g_late_patched;
        }
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
                const bool posed = g_fix_frame + 1 == g_frame && !g_fixes.empty();  // last frame's result
                g_pose_flips += posed != g_posed_prev;
                g_posed_prev = posed;
                g_fix_frame = g_frame;
                g_fixes.clear();
                g_first_start = ~0u;
                g_seen_now.clear();
                g_tries_now = 0;
            }
            if (g_multi) {
                // Any piece of the layout places all of it (one bone buffer, fixed rows apart).
                for (size_t p = 0; p < g_layout.counts.size() && g_fixes.empty() && g_tries_now < 8; ++p) {
                    if (count != g_layout.counts[p]) continue;
                    ++g_tries_now;
                    static std::vector<arm_solve::Bone> merged;  // render thread only
                    if (gather(buf, (long long)(fe + start) - g_layout.rel[p], merged) &&
                        compute_rows(merged[0].m, int(merged.size())) && !marked)
                        ++g_early;
                    break;
                }
            } else {
                // Her first marked piece: the arm block where it was last frame, from it (D-044).
                if (marked && g_first_start == ~0u) {
                    g_first_start = start;
                    if (!arm && g_arm_rel >= 0 && g_fixes.empty())
                        compute(buf, (fe + start + g_arm_rel + kHeaderRows) * 16, int(g_arm_count - kHeaderRows) / 3);
                }
                if (g_fixes.empty()) {
                    const auto rel = g_rel_by_count.find(count);
                    if (arm) {
                        if (compute(buf, (fe + start + kHeaderRows) * 16, int(g_arm_count - kHeaderRows) / 3))
                            for (const Seen& e : g_seen_now)  // where the arm block is from each piece drawn before it
                                if (e.buf == buf) g_rel_by_count[e.count] = (long long)(fe + start) - (long long)e.row;
                    } else if (!marked && rel != g_rel_by_count.end() && g_tries_now < 8 && (long long)(fe + start) + rel->second >= 0) {
                        ++g_tries_now;
                        if (compute(buf, ((fe + start) + rel->second + kHeaderRows) * 16, int(g_arm_count - kHeaderRows) / 3)) ++g_early;
                    }
                    if (g_fixes.empty() && g_seen_now.size() < 64) g_seen_now.push_back({buf, fe + start, count});
                }
                if (arm && marked && g_first_start != ~0u) g_arm_rel = int(start - g_first_start);
            }
            have = !g_fixes.empty();
            g_pieces[idx].patched = have;
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
        // Her biggest piece (the arms, 81 bones, STAGE7-RECON) from the bone buffer shadow; marked pieces only (once
        // the IK runs, others' skinned draws are taken too).
        const Piece* big = &g_pieces[0];
        for (const Piece& p : g_pieces)
            if (p.marked && (!big->marked || p.count > big->count)) big = &p;
        const auto* v = contents(big->buf);
        const size_t from = (size_t(big->start) + kHeaderRows) * 16;
        const unsigned bones = (big->count - kHeaderRows) / 3;
        if (v && big->marked && from + bones * 48 <= v->size()) {
            if (!g_rec) _wfopen_s(&g_rec, (g_dir / L"bones_rec.bin").c_str(), L"wb");
            if (g_rec) {
                fwrite(&bones, 4, 1, g_rec);
                fwrite(v->data() + from, 48, bones, g_rec);
                ++g_recorded;
            }
            // Every marked piece of the frame too (game pose), for which of their rows copy an arm bone (BUG-020).
            if (!g_rec_pieces) _wfopen_s(&g_rec_pieces, (g_dir / L"pieces_rec.bin").c_str(), L"wb");
            if (g_rec_pieces) {
                std::vector<const Piece*> keep;
                for (const Piece& p : g_pieces) {
                    const auto* pv = p.marked ? contents(p.buf) : nullptr;
                    if (pv && (size_t(p.start) + p.count) * 16 <= pv->size()) keep.push_back(&p);
                }
                const unsigned n = unsigned(keep.size());
                fwrite(&n, 4, 1, g_rec_pieces);
                for (const Piece* p : keep) {
                    const unsigned pb = (p->count - kHeaderRows) / 3;
                    fwrite(&pb, 4, 1, g_rec_pieces);
                    fwrite(contents(p->buf)->data() + (size_t(p->start) + kHeaderRows) * 16, 48, pb, g_rec_pieces);
                }
            }
        }
        if (--g_record_left <= 0 && g_rec) {
            fclose(g_rec);
            g_rec = nullptr;
            if (g_rec_pieces) fclose(g_rec_pieces);
            g_rec_pieces = nullptr;
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
        // 7A: the arms are the biggest piece (81 bones, STAGE7-RECON); feed its matrices to the learner. PLAYTEST-8n:
        // all her marked pieces together when it's too small for both arms.
        const Piece* big = &g_pieces[0];
        for (const Piece& p : g_pieces)
            if (p.count > big->count) big = &p;
        const auto* v = contents(big->buf);
        const size_t from = (size_t(big->start) + kHeaderRows) * 16;
        const bool multi = int(big->count - kHeaderRows) / 3 < kOnePieceBones;
        static std::vector<arm_solve::Bone> merged;  // render thread only
        const arm_solve::Bone* fb = nullptr;
        int bones = 0;
        if (multi) {
            if (layout_frame(merged, true)) {
                fb = merged.data();
                bones = int(merged.size());
            }
        } else if (v && from + size_t(big->count - kHeaderRows) * 16 <= v->size()) {
            fb = reinterpret_cast<const arm_solve::Bone*>(v->data() + from);
            bones = int(big->count - kHeaderRows) / 3;
        }
        if (fb && bones >= 20) {
            if (g_learner.bones() != bones || g_multi != multi) g_learner.reset(bones);
            g_multi = multi;
            g_learner.ride_check = multi;  // D-072: free roam's 81-bone piece solves as before the jacket work
            g_learner.add(fb);
            if (g_learner.frames() >= kLearnFrames) {
                const double t0 = now_ms();
                g_arms = g_learner.solve();
                ++g_tries;
                if (g_tries <= 5 || g_tries % 10 == 0 || g_arms.ok)
                LOG_I("bones", "arms from %d frames of the %d-bone %s (try %d, %.0f ms; %d joints, %d wrist candidates, %d arm chains): %s",
                      g_learner.frames(), bones, multi ? "skeleton of her pieces" : "piece", g_tries, now_ms() - t0, g_arms.joints,
                      g_arms.wrists, g_arms.chains, g_arms.why);
                if (!g_arms.ok && g_tries <= 5)
                    for (const auto& c : g_arms.found)
                        LOG_I("bones", "  chain found (bind space): shoulder %.3f %.3f %.3f, elbow %.3f %.3f %.3f, wrist %.3f %.3f %.3f",
                              c.shoulder.x, c.shoulder.y, c.shoulder.z, c.elbow.x, c.elbow.y, c.elbow.z, c.wrist.x, c.wrist.y,
                              c.wrist.z);
                if (g_arms.ok)
                    for (int a = 0; a < 2; ++a) {
                        const auto& arm = g_arms.arm[a];
                        const arm_solve::Bone* b = fb;
                        const vm::Vec3 s = b[arm.shoulder_bone].apply(arm.shoulder), e = b[arm.elbow_bone].apply(arm.elbow),
                                       w = b[arm.wrist_bone].apply(arm.wrist);
                        int n[3] = {0, 0, 0};
                        for (int k = 0; k < bones; ++k)
                            if (g_arms.side[k] == a) ++n[g_arms.segment[k]];
                        char list[2][96] = {"", ""};  // which bones (PLAYTEST-7g: a chest bone on the upper arm)
                        for (int seg = 0; seg < 2; ++seg) {
                            size_t len = 0;
                            for (int k = 0; k < bones && len < sizeof(list[seg]) - 5; ++k)
                                if (g_arms.side[k] == a && g_arms.segment[k] == seg) len += snprintf(list[seg] + len, sizeof(list[seg]) - len, " %d", k);
                        }
                        LOG_I("bones", "  arm %d: upper arm %.3f m, forearm %.3f m; bones upper %d (%s), forearm %d (%s), hand %d", a,
                              (s - e).length(), (e - w).length(), n[0], list[0] + (list[0][0] == ' '), n[1], list[1] + (list[1][0] == ' '), n[2]);
                        if (arm.hand_bone >= 0 && arm.fore_bone >= 0) {  // 2026-10-28 (right glove torn): how each forearm bone carries a palm point
                            char fl[256] = "";
                            size_t len = 0;
                            const vm::Vec3 palm = arm.wrist + (arm.wrist - arm.elbow).normalized() * 0.08f;
                            for (int k = 0; k < bones && len < sizeof(fl) - 16; ++k)
                                if (g_arms.side[k] == a && g_arms.segment[k] == arm_solve::kFore)
                                    len += snprintf(fl + len, sizeof(fl) - len, " %d:%.0f/%.0f", k, 1000 * g_learner.apart(k, arm.fore_bone, palm),
                                                    1000 * g_learner.apart(k, arm.hand_bone, palm));
                            LOG_I("bones", "    its forearm bones, a palm point against forearm bone %d / hand bone %d (mm):%s", arm.fore_bone, arm.hand_bone, fl);
                        }
                        // 7D: each bone's joint with the hand bone, for the fingers' first knuckles (finger_rig).
                        g_fits[a].at.assign(bones, {});
                        g_fits[a].rms.assign(bones, -1);
                        for (int k = 0; k < bones; ++k) {
                            double rms;
                            if (arm.hand_bone >= 0 && g_learner.joint(arm.hand_bone, k, g_fits[a].at[k], rms)) g_fits[a].rms[k] = rms;
                        }
                    }
                if (g_arms.ok && multi) {
                    // The fingers come from the piece with most hand bones.
                    int most = 0;
                    for (size_t p = 0; p < g_layout.counts.size(); ++p) {
                        int n = 0;
                        for (int u : g_layout.merge.of[p]) n += g_arms.side[u] >= 0 && g_arms.segment[u] == arm_solve::kHand;
                        if (n > most) { most = n; g_layout.hand_piece = int(p); }
                    }
                    char line[160] = "";
                    size_t len = 0;
                    for (size_t p = 0; p < g_layout.counts.size() && len < sizeof(line) - 24; ++p) {
                        int seg[3] = {0, 0, 0};
                        for (int u : g_layout.merge.of[p])
                            if (g_arms.side[u] >= 0) ++seg[g_arms.segment[u]];
                        len += snprintf(line + len, sizeof(line) - len, " %u:%d/%d/%d", (g_layout.counts[p] - kHeaderRows) / 3, seg[0], seg[1], seg[2]);
                    }
                    LOG_I("bones", "  her arms over %zu pieces (bones, then its upper arm/forearm/hand bones):%s; fingers from piece %d", g_layout.counts.size(),
                          line, g_layout.hand_piece + 1);
                }
                g_learning = !g_arms.ok && g_tries < kLearnTries;
                g_arm_count = multi ? 0 : big->count;
                g_ik_ready = g_arms.ok && g_arms_on.load();
                if (!g_arms.ok) g_learner.reset(bones);  // else it goes on learning (below)
                g_copy_ms = g_copy_max_ms = 0;
                g_copies = 0;

            }
        }
    }
    if (g_ik_ready.load() && g_refines < kRefineWindows &&
        g_learner.bones() == (g_multi ? int(g_layout.merge.src.size()) : int(g_arm_count - kHeaderRows) / 3)) {
        const Piece* arm = nullptr;
        for (const Piece& p : g_pieces)
            if (p.marked && p.count == g_arm_count) { arm = &p; break; }
        const auto* v = arm ? contents(arm->buf) : nullptr;
        const size_t from = arm ? (size_t(arm->start) + kHeaderRows) * 16 : 0;
        const int bones = g_learner.bones();
        static std::vector<arm_solve::Bone> merged;  // render thread only
        const arm_solve::Bone* fb = g_multi ? (layout_frame(merged, false) ? merged.data() : nullptr)
                                    : v && from + size_t(bones) * 48 <= v->size() ? reinterpret_cast<const arm_solve::Bone*>(v->data() + from)
                                                                                  : nullptr;
        if (fb) {
            g_learner.add(fb);
            if (g_learner.frames() >= kRefineEvery) {
                const arm_solve::Result next = g_learner.solve();
                arm_solve::Result trial = g_arms;
                int taken = arm_solve::adopt_new_bones(trial, next);
                g_learner.reset(bones);
                ++g_refines;
                if (taken > 0) {  // only bones two windows place the same way (one bad window can't add a chest bone)
                    g_pending.resize(size_t(bones), -1);
                    taken = 0;
                    for (int k = 0; k < bones; ++k) {
                        if (g_arms.side[k] >= 0) continue;
                        const int code = trial.side[k] >= 0 ? trial.side[k] * 4 + trial.segment[k] : -1;
                        if (code >= 0 && code == g_pending[k]) {
                            g_arms.side[k] = trial.side[k];
                            g_arms.segment[k] = trial.segment[k];
                            ++taken;
                        }
                        g_pending[k] = code;
                    }
                }
                ++(!next.ok ? g_refine_failed : taken < 0 ? g_refine_other : g_refine_same);
                int hand[2] = {0, 0};
                for (int k = 0; k < bones; ++k)
                    if (g_arms.side[k] >= 0 && g_arms.segment[k] == arm_solve::kHand) ++hand[g_arms.side[k]];
                if (taken > 0)
                    LOG_I("bones", "arms again (window %d): %d more bones placed; hand bones now %d and %d", g_refines, taken, hand[0], hand[1]);
                if (g_refines % 10 == 0) {
                    LOG_I("bones", "arms again, windows %d-%d: %d agreed, %d other arms (kept ours), %d no arms found; hand bones %d and %d",
                          g_refines - 9, g_refines, g_refine_same, g_refine_other, g_refine_failed, hand[0], hand[1]);
                    g_refine_same = g_refine_other = g_refine_failed = 0;
                }
            }
        }
    }
    static ULONGLONG ik_log = 0;
    if (g_ik_ready.load() && GetTickCount64() - ik_log >= 5000) {
        ik_log = GetTickCount64();
        LOG_I("bones", "IK arms in 5 s: %d frames, %d arms posed (%d out of reach, clamped), %d hands too far; hand twist about "
              "the forearm %.0f deg avg, forearm takes %.2f; %d rows of other pieces followed (%d riding with a nearby arm bone)", g_ik_frames, g_ik_arms, g_ik_clamped, g_ik_far,
              g_ik_arms ? g_twist_sum / g_ik_arms : 0.f, g_forearm_twist.load(), g_patched_other, g_carried);
        g_carried = 0;
        if (g_curl_n)
            LOG_I("bones", "  fingers posed on %d arms: index curl %.2f avg, the others %.2f", g_curl_n, g_curl_sum[0] / g_curl_n,
                  g_curl_sum[1] / g_curl_n);
        g_curl_sum[0] = g_curl_sum[1] = 0;
        g_curl_n = 0;
        if (g_hang_frames) LOG_I("bones", "  %d frames hanging (game's arms)", g_hang_frames);
        if (g_jacket_held_frames) LOG_I("bones", "  %d frames her jacket unmarked (game's arms)", g_jacket_held_frames);
        g_jacket_held_frames = 0;
        if (g_hand_up_frames) LOG_I("bones", "  %d frames a game hand above the shoulder (no room-scale walking)", g_hand_up_frames);
        g_hang_frames = g_hand_up_frames = 0;
        if (g_ik_far)
            LOG_I("bones", "  hands too far: %.2f-%.2f m from the shoulder (mono %d, left eye %d, right eye %d)", g_far_min,
                  g_far_max, g_far_eye[0], g_far_eye[1], g_far_eye[2]);
        if (g_ik_arms)
            LOG_I("bones", "  posed hands up to %.2f m from the shoulder; your reach %.2f m, hers %.2f m, hands pulled in up to %.0f mm%s",
                  g_near_max, g_your_reach, g_her_reach, g_pulled_max * 1000.f, g_reach_scale.load() ? "" : " (reach scale off)");
        g_pulled_max = 0;
        if (g_ik_frames)
            LOG_I("bones", "  her shoulders turned from the view up to %.0f deg (%d frames past 20); %d posed elbows inside her chest (elbows follow the hand %.2f)",
                  g_torso_turn_max, g_torso_turned, g_elbow_in_chest, g_elbow_follow_hand.load());
        g_torso_turn_max = 0;
        g_torso_turned = g_elbow_in_chest = 0;
        if (g_body_frames)
            LOG_I("bones", "  attacks: her body turned back in %d frames, up to %.0f deg; %d rows of her other pieces turned with it",
                  g_body_frames, g_body_max, g_turned_rows);
        g_body_frames = g_turned_rows = 0;
        for (int k = 0; k < 2; ++k)
            if (g_off_hands_n[k])
                LOG_I("bones", "  %s: her chest off your hands' direction up to %.0f deg (%d of %d frames past 30)",
                      k == 0 ? "attacks" : "no attack", g_off_hands_max[k], g_off_hands_30[k], g_off_hands_n[k]);
        g_off_hands_max[0] = g_off_hands_max[1] = 0;
        LOG_I("bones", "  her shoulders against the view: %d jumps past 5 cm between frames (largest %.2f m); posed <-> game pose %d times; "
              "forearm twist kept from flipping past 180 deg in %d frames, held (her hand ~180 deg off yours) in %d",
              g_rel_jumps, g_rel_jump_max, g_pose_flips, g_twist_kept, g_twist_held);
        g_rel_jumps = g_pose_flips = g_twist_kept = g_twist_held = 0;
        g_rel_jump_max = 0;
        g_off_hands_n[0] = g_off_hands_n[1] = g_off_hands_30[0] = g_off_hands_30[1] = 0;
        if (g_late_patched) LOG_I("bones", "  %d pieces drawn before the solve were patched at a later draw", g_late_patched);
        g_late_patched = 0;
        g_body_max = 0;
        if (g_not_arms) LOG_I("bones", "  %d blocks looked for the arms weren't them", g_not_arms);
        if (g_no_hands || g_stale || g_no_arm_piece) {
            char sizes[96] = "";
            size_t len = 0;
            for (unsigned i = 0; i < g_seen_n && len < sizeof(sizes) - 16; ++i)
                len += snprintf(sizes + len, sizeof(sizes) - len, " %u", (g_seen_counts[i] - kHeaderRows) / 3);
            LOG_I("bones", "  frames not posed: %d no hands, %d bone block not written this frame, %d without the %u-bone arm piece (bones per piece then:%s)",
                  g_no_hands, g_stale, g_no_arm_piece, g_arm_count ? (g_arm_count - kHeaderRows) / 3 : 0u, sizes);
        }
        if (g_unmarked_frames)
            LOG_I("bones", "  %d frames her body wasn't marked (ledge hang or other); IK before the arms' own draw from a piece's offset in %d frames",
                  g_unmarked_frames, g_early);
        g_unmarked_frames = g_early = 0;
        if (g_no_faith) LOG_I("bones", "  %d frames without any piece of Faith", g_no_faith);
        g_no_faith = 0;
        g_not_arms = g_no_hands = g_stale = g_no_arm_piece = 0;
        g_seen_n = 0;
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
    if (g_ik_ready.load() && g_pieces.empty()) ++g_no_faith;
    g_unmarked = !g_marked_now;
    if (!g_marked_now && g_ik_ready.load()) ++g_unmarked_frames;
    {  // BUG-039: her jacket only (free roam's arm piece finds her arms unmarked as before)
        const bool held = g_ik_ready.load() && g_multi && g_jacket_hold.frame(g_marked_now);
        if (!(g_ik_ready.load() && g_multi)) g_jacket_hold = {};
        if (held != g_jacket_held)
            LOG_I("bones", held ? "her jacket isn't marked (wall-run or other): arms go to the game's animation"
                                : "her jacket is marked again: arms follow yours again");
        g_jacket_held = held;
        g_jacket_held_frames += held;
    }
    g_marked_now = false;
    if (g_ik_ready.load() && !g_multi && !g_pieces.empty()) {
        bool arm = false;
        for (const Piece& p : g_pieces) arm |= p.count == g_arm_count;
        if (!arm) {
            ++g_no_arm_piece;
            for (const Piece& p : g_pieces) {
                bool known = false;
                for (unsigned i = 0; i < g_seen_n; ++i) known |= g_seen_counts[i] == p.count;
                if (!known && g_seen_n < 4) g_seen_counts[g_seen_n++] = p.count;
            }
        }
    }
    if (g_ik_ready.load()) {  // her outfit changed (no arms in her marked pieces for ~30 s): learn them again
        std::vector<unsigned> counts;
        for (const Piece& p : g_pieces)
            if (p.marked) counts.push_back(p.count);
        const camera_override::Hands h = camera_override::rendered_hands();
        if (g_arms_at == g_frame) g_gone_run = 0;
        else if (!counts.empty() && (h.valid[0] || h.valid[1])) ++g_gone_run;
        if (!counts.empty()) {
            std::vector<int> bones, learned;
            for (unsigned c : counts) bones.push_back(int(c - kHeaderRows) / 3);
            if (g_multi)
                for (unsigned k : g_layout.counts) learned.push_back(int(k - kHeaderRows) / 3);
            else
                learned.push_back(int(g_arm_count - kHeaderRows) / 3);
            const bool known = arm_solve::outfit_known(bones, learned, g_multi, kOnePieceBones);
            g_outfit_run = known ? 0 : g_outfit_run + 1;
        }
        if (g_gone_run >= kRelearnFrames || g_outfit_run >= kOutfitFrames) {
            char line[96] = "";
            size_t len = 0;
            for (size_t i = 0; i < counts.size() && len < sizeof(line) - 8; ++i)
                len += snprintf(line + len, sizeof(line) - len, " %u", (counts[i] - kHeaderRows) / 3);
            if (g_outfit_run >= kOutfitFrames)
                LOG_I("bones", "her pieces are other sizes than the arms were learned on for %d frames (bones per piece now:%s): her outfit changed, learning them again",
                      g_outfit_run, line);
            else
                LOG_I("bones", "her arms weren't in her pieces for %d frames (bones per piece now:%s): learning them again", g_gone_run, line);
            g_arms = {};
            g_ik_ready = false;
            g_learning = g_arms_on.load();
            g_tries = g_refines = g_gone_run = g_outfit_run = 0;
            g_learner.reset(0);
            g_multi = false;
            g_layout = g_layout_next = {};
            g_layout_frames.clear();
            g_arm_count = 0;
            g_arm_rel = -1;
            g_rel_by_count.clear();
            g_left = -1;
            g_vote = 0;
            g_votes = 0;
            g_hand_ok[0] = g_hand_ok[1] = false;
            g_finger_state = 0;
            g_finger_of.clear();
            g_rig[0] = g_rig[1] = {};
            g_fits[0] = g_fits[1] = {};
            g_pending.clear();
            g_fixes.clear();
            g_hanging = false;
            g_ik_weight = 1.f;
        }
    }
    ++g_frame;
    g_last_pieces.swap(g_pieces);
    g_pieces.clear();
}

}  // namespace mecvr::faith_bones

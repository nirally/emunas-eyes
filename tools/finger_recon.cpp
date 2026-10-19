// Stage 7D recon: Faith's finger bones from recorded skinning matrices (bones_rec.bin, the 81-bone arm piece).
//
// arm_solve finds the arms and which bones ride on each hand. Here, per hand: every pair of its bones (plus the
// palm bone) that shares a joint point (fit within 2 mm, rotating against each other) -> the knuckles; the
// bones are then chained outward from the palm bone (the one turning on the wrist) through those knuckles.
// For each bone: its knuckle in bind space relative to the wrist, in the hand's frame (arm_ik::hand_frame axes),
// and how far the game turns it against the palm bone over the recording (min / mean / max degrees, and the
// axis of the mean turn in the hand's frame) - the game's own finger curl.
//
// With the arm piece's mesh (MESHDUMP mesh_vb0.bin, and how many vertices its draw uses) also each bone's skin
// and the runtime finger rig (finger_rig) on it (docs/recon/STAGE7D-RECON.md).
//
// Usage: finger_recon <bones_rec.bin> [mesh_vb0.bin [vertices]]
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include "core/arm_ik.h"
#include "core/arm_solve.h"
#include "core/finger_rig.h"

using mecvr::arm_solve::Bone;
namespace vm = mecvr::vm;

std::vector<unsigned char> g_vb;  // the mesh's vertices (optional)
int g_verts = 0;

struct Fit { bool ok = false; vm::Vec3 x; double rms = 1e9; };

static Fit joint(const std::vector<std::vector<Bone>>& fr, int i, int j) {
    double ata[3][3] = {}, atb[3] = {}, btb = 0;
    for (const auto& b : fr)
        for (int r = 0; r < 3; ++r) {
            const double row[3] = {double(b[i].m[r * 4]) - b[j].m[r * 4], double(b[i].m[r * 4 + 1]) - b[j].m[r * 4 + 1],
                                   double(b[i].m[r * 4 + 2]) - b[j].m[r * 4 + 2]};
            const double rhs = double(b[j].m[r * 4 + 3]) - b[i].m[r * 4 + 3];
            btb += rhs * rhs;
            for (int p = 0; p < 3; ++p) {
                atb[p] += row[p] * rhs;
                for (int q = p; q < 3; ++q) ata[p][q] += row[p] * row[q];
            }
        }
    Fit f;
    double x[3];
    if (!mecvr::arm_solve::detail::solve3(ata, atb, 1e-4 * fr.size(), x)) return f;
    double r2 = btb;
    for (int p = 0; p < 3; ++p) {
        r2 -= 2 * x[p] * atb[p];
        for (int q = 0; q < 3; ++q) r2 += x[p] * (p <= q ? ata[p][q] : ata[q][p]) * x[q];
    }
    f.rms = std::sqrt(std::fmax(r2, 0.0) / fr.size());
    f.x = {float(x[0]), float(x[1]), float(x[2])};
    f.ok = f.rms < (getenv("FIT_MM") ? atof(getenv("FIT_MM")) : 2.0) * 0.001;
    return f;
}

static vm::Quat rot_of(const Bone& b) {
    vm::LinearTransform lt{};
    for (int c = 0; c < 3; ++c) {
        lt.right[c] = b.m[c * 4];
        lt.up[c] = b.m[c * 4 + 1];
        lt.back[c] = b.m[c * 4 + 2];
    }
    return vm::quat_of(lt);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: finger_recon <bones_rec.bin>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<std::vector<Bone>> fr;
    uint32_t n = 0, bones = 0;
    while (fread(&n, 4, 1, f) == 1 && n > 0 && n < 512) {
        std::vector<Bone> b(n);
        if (fread(b.data(), sizeof(Bone), n, f) != n) break;
        if (bones && n != bones) continue;
        bones = n;
        fr.push_back(std::move(b));
    }
    fclose(f);
    printf("%zu frames of %u bones\n", fr.size(), bones);
    // Optional mesh (MESHDUMP mesh_vb0.bin, 60-byte vertices: float3 bind position at 0, 8 bone indices at 20,
    // 8 weights at 28 summing to 255): per bone, the weighted skin centroid and the vertex (weight > half) farthest
    // from the wrist.
    std::vector<vm::Vec3> mesh_c(bones), mesh_far(bones);
    std::vector<double> mesh_w(argc > 2 ? bones : 0);
    if (argc > 2) {
        FILE* mf = fopen(argv[2], "rb");
        if (!mf) { perror(argv[2]); return 1; }
        std::vector<unsigned char> vb;
        unsigned char buf[60];
        while (fread(buf, 1, 60, mf) == 60) vb.insert(vb.end(), buf, buf + 60);
        fclose(mf);
        const int verts = argc > 3 ? atoi(argv[3]) : int(vb.size() / 60);
        for (int v = 0; v < verts && size_t(v + 1) * 60 <= vb.size(); ++v) {
            const unsigned char* p = &vb[size_t(v) * 60];
            vm::Vec3 x;
            memcpy(&x, p, 12);
            for (int k = 0; k < 8; ++k) {
                const int b = p[20 + k] + 1;  // vertex bone v = row v + 1 (row 0: identity)
                const float w = p[28 + k] / 255.f;
                if (w <= 0 || b >= int(bones)) continue;
                mesh_c[b] = mesh_c[b] + x * w;
                mesh_w[b] += w;
            }
        }
        printf("mesh: %d vertices\n", verts);
        g_vb = std::move(vb);
        g_verts = verts;
    }
    mecvr::arm_solve::Learner L;
    L.reset(int(bones));
    for (const auto& b : fr) L.add(b.data());
    const auto res = L.solve();
    printf("arm solve: %s\n", res.why);
    if (!res.ok) return 1;

    for (int a = 0; a < 2; ++a) {
        const auto& arm = res.arm[a];
        // The hand's frame (the glove's hand space), her front from the two shoulders as faith_bones does.
        const vm::Vec3 front = vm::Vec3{0, 1, 0}.cross(res.arm[1 - a].shoulder - arm.shoulder).normalized();
        vm::Quat hb;
        const bool hf = mecvr::arm_ik::hand_frame(arm.wrist, arm.hand_joints, front, hb);
        const vm::Quat hinv = hb.conjugate();
        std::vector<int> hand;
        for (int b = 0; b < int(bones); ++b)
            if (res.side[b] == a && res.segment[b] == mecvr::arm_solve::kHand) hand.push_back(b);
        printf("\n=== arm %d: %zu hand bones, palm bone %d, hand frame %s; wrist (bind) %.3f %.3f %.3f\n", a, hand.size(),
               arm.hand_bone, hf ? "ok" : "NOT FOUND", arm.wrist.x, arm.wrist.y, arm.wrist.z);
        printf("shoulder %.3f %.3f %.3f, elbow %.3f %.3f %.3f\n", arm.shoulder.x, arm.shoulder.y, arm.shoulder.z, arm.elbow.x,
               arm.elbow.y, arm.elbow.z);
        for (int seg = 0; seg < 2; ++seg) {
            printf("%s bones:", seg ? "forearm" : "upper arm");
            for (int b = 0; b < int(bones); ++b)
                if (res.side[b] == a && res.segment[b] == seg) printf(" %d", b);
            printf("\n");
        }
        // How far each bone near the shoulder puts her elbow from where the upper arm bones put it (rms over the
        // recording): bones riding with the arm keep it; her chest and collarbone don't.
        printf("elbow apart from upper arm bones (cm):");
        for (int b : {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 41, 56, 57, 58, 59})
            printf(" %d: %.1f/%.1f", b, L.apart(b, a == 0 ? 13 : 56, arm.elbow) * 100, L.apart(b, a == 0 ? 20 : 58, arm.elbow) * 100);
        printf("\n");
        // Knuckles: pair joints among the hand bones.
        struct Edge { int p, c; vm::Vec3 x; double rms; };
        std::vector<Edge> edges;
        for (size_t i = 0; i < hand.size(); ++i)
            for (size_t j = i + 1; j < hand.size(); ++j) {
                const Fit ft = joint(fr, hand[i], hand[j]);
                if (ft.ok) edges.push_back({hand[i], hand[j], ft.x, ft.rms});
            }
        // Chain outward from the palm bone: BFS; a bone's parent = the reached neighbour whose shared joint is
        // nearer the wrist.
        std::vector<int> parent(bones, -2), depth(bones, -1);
        std::vector<vm::Vec3> knuckle(bones);
        parent[arm.hand_bone] = -1;
        depth[arm.hand_bone] = 0;
        knuckle[arm.hand_bone] = arm.wrist;
        for (bool grew = true; grew;) {
            grew = false;
            for (const Edge& e : edges)
                for (int s = 0; s < 2; ++s) {
                    const int from = s ? e.c : e.p, to = s ? e.p : e.c;
                    if (depth[from] < 0 || depth[to] >= 0) continue;
                    // the joint must lie further from the wrist than `from`'s own knuckle (outward)
                    if ((e.x - arm.wrist).length() + 0.003f < (knuckle[from] - arm.wrist).length()) continue;
                    parent[to] = from;
                    depth[to] = depth[from] + 1;
                    knuckle[to] = e.x;
                    grew = true;
                }
        }
        // The game's turn of each bone against the palm bone over the recording.
        for (int b : hand) {
            double mn = 1e9, mx = 0, sum = 0;
            vm::Vec3 axis_sum{0, 0, 0};
            for (const auto& frm : fr) {
                const vm::Quat rel = (rot_of(frm[arm.hand_bone]).conjugate() * rot_of(frm[b])).normalized();
                const float w = std::fmin(1.f, std::fabs(rel.w));
                const double ang = 2 * std::acos(w) * 57.29578;
                mn = std::fmin(mn, ang);
                mx = std::fmax(mx, ang);
                sum += ang;
                vm::Vec3 ax{rel.x, rel.y, rel.z};
                if (rel.w < 0) ax = ax * -1.f;
                axis_sum = axis_sum + hinv.rotate(ax);  // in the hand frame (bind and world turns agree: same R_hand^T)
            }
            const vm::Vec3 k = hinv.rotate(knuckle[b] - arm.wrist);
            const vm::Vec3 ax = axis_sum.normalized();
            printf("bone %2d depth %2d parent %3d knuckle (hand frame) %+.3f %+.3f %+.3f  game turn vs palm %5.1f/%5.1f/%5.1f deg, axis %+.2f %+.2f %+.2f\n",
                   b, depth[b], parent[b], k.x, k.y, k.z, mn, sum / fr.size(), mx, ax.x, ax.y, ax.z);
        }
        for (int v = 0; v < g_verts; ++v) {
            const unsigned char* p = &g_vb[size_t(v) * 60];
            vm::Vec3 x;
            memcpy(&x, p, 12);
            for (int k = 0; k < 8; ++k) {
                const int b = p[20 + k] + 1;  // vertex bone v = row v + 1 (row 0: identity)
                if (p[28 + k] < 128 || b >= int(bones) || res.side[b] != a) continue;
                if ((x - arm.wrist).length() > (mesh_far[b] - arm.wrist).length() || mesh_far[b].length() == 0) mesh_far[b] = x;
            }
        }
        if (!mesh_w.empty()) {  // every arm bone: segment and its skin centroid's distance from wrist / elbow / shoulder
            printf("arm bones' skin: bone segment | from wrist, elbow, shoulder (m)\n");
            for (int b = 0; b < int(bones); ++b) {
                if (res.side[b] != a || mesh_w[b] <= 0) continue;
                const vm::Vec3 c = mesh_c[b] * (1.f / float(mesh_w[b]));
                printf("  %2d %d | %.3f %.3f %.3f  w %.0f\n", b, res.segment[b], (c - arm.wrist).length(), (c - arm.elbow).length(),
                       (c - arm.shoulder).length(), mesh_w[b]);
            }
        }
        if (!mesh_w.empty()) {  // each hand bone's skin (MESHDUMP): weighted centroid and farthest reach, hand frame
            printf("skin (mesh):\n");
            for (int b : hand) {
                if (mesh_w[b] <= 0) { printf("  %2d: no skin\n", b); continue; }
                const vm::Vec3 c = hinv.rotate(mesh_c[b] * (1.f / float(mesh_w[b])) - arm.wrist);
                const vm::Vec3 t = hinv.rotate(mesh_far[b] - arm.wrist);
                printf("  %2d: centroid %+.3f %+.3f %+.3f, farthest %+.3f %+.3f %+.3f, weight %.0f\n", b, c.x, c.y, c.z, t.x, t.y,
                       t.z, mesh_w[b]);
            }
        }
        printf("knuckle edges (%zu):\n", edges.size());
        for (const Edge& e : edges) {
            const vm::Vec3 k = hinv.rotate(e.x - arm.wrist);
            printf("  %2d-%2d at %+.3f %+.3f %+.3f (%.1f mm rms)\n", e.p, e.c, k.x, k.y, k.z, e.rms * 1000);
        }
        // Best partners per bone (joint fit rms), and which bones the game turns together: correlation of their
        // turn angles against the palm over the recording (one finger's bones curl together).
        printf("best partners:\n");
        for (int b : hand) {
            std::vector<std::pair<double, int>> p;
            for (int o : hand)
                if (o != b) p.push_back({joint(fr, b, o).rms, o});
            std::sort(p.begin(), p.end());
            printf("  %2d:", b);
            for (size_t i = 0; i < 3 && i < p.size(); ++i) printf(" %2d (%.1f mm)", p[i].second, p[i].first * 1000);
            printf("\n");
        }
        std::vector<int> moving;
        std::vector<std::vector<double>> ang;
        for (int b : hand) {
            std::vector<double> s;
            double mean = 0;
            for (const auto& frm : fr) {
                const vm::Quat rel = (rot_of(frm[arm.hand_bone]).conjugate() * rot_of(frm[b])).normalized();
                s.push_back(2 * std::acos(std::fmin(1.f, std::fabs(rel.w))) * 57.29578);
                mean += s.back();
            }
            if (mean / fr.size() < 5) continue;
            moving.push_back(b);
            ang.push_back(s);
        }
        printf("turn correlation (bones turning 5+ deg on average):\n     ");
        for (int b : moving) printf(" %4d", b);
        printf("\n");
        for (size_t i = 0; i < moving.size(); ++i) {
            printf("  %2d:", moving[i]);
            for (size_t j = 0; j < moving.size(); ++j) {
                double mi = 0, mj = 0, sij = 0, sii = 0, sjj = 0;
                const size_t N = ang[i].size();
                for (size_t t = 0; t < N; ++t) { mi += ang[i][t]; mj += ang[j][t]; }
                mi /= N;
                mj /= N;
                for (size_t t = 0; t < N; ++t) {
                    sij += (ang[i][t] - mi) * (ang[j][t] - mj);
                    sii += (ang[i][t] - mi) * (ang[i][t] - mi);
                    sjj += (ang[j][t] - mj) * (ang[j][t] - mj);
                }
                printf(" %+.1f", sij / std::sqrt(sii * sjj + 1e-12));
            }
            printf("\n");
        }
    }
    // The runtime rig (finger_rig) on this mesh: fingers, knuckles, axes; and where each finger's tip skin goes
    // closed vs open (relative to the wrist, in bind space).
    if (g_verts > 0) {
        std::vector<mecvr::finger_rig::Vertex> verts;
        const bool parsed = mecvr::finger_rig::parse(g_vb.data(), g_vb.size(), 60, 0, unsigned(g_verts), int(bones), verts);
        printf("\nfinger_rig: layout %s, %zu vertices\n", parsed ? "ok" : "NOT RECOGNISED", verts.size());
        // Her front: up x (left shoulder -> right shoulder); her left is the arm at +x (live: "Faith's left arm is arm 1").
        const int left = res.arm[0].wrist.x > 0 ? 0 : 1;
        const vm::Vec3 front = vm::Vec3{0, 1, 0}.cross(res.arm[1 - left].shoulder - res.arm[left].shoulder).normalized();
        printf("front %+.2f %+.2f %+.2f\n", front.x, front.y, front.z);
        static const char* names[4] = {"index", "middle", "ring", "pinky"};
        for (int a = 0; a < 2; ++a) {
            mecvr::finger_rig::Fits fits;
            fits.at.assign(bones, {});
            fits.rms.assign(bones, -1);
            for (int b = 0; b < int(bones); ++b) {
                double rms;
                if (L.joint(res.arm[a].hand_bone, b, fits.at[b], rms)) fits.rms[b] = rms;
            }
            const auto hand = mecvr::finger_rig::build(verts, res, a, front, &fits);
            printf("arm %d: %s\n", a, hand.why);
            if (!hand.ok) continue;
            const vm::Vec3 w = res.arm[a].wrist;
            for (int f = 0; f < 4; ++f) {
                const auto& fg = hand.finger[f];
                printf("  %-6s bones %2d %2d %2d  MCP %+.3f %+.3f %+.3f  PIP %+.3f %+.3f %+.3f  DIP %+.3f %+.3f %+.3f  axis %+.2f %+.2f %+.2f\n",
                       names[f], fg.bone[0], fg.bone[1], fg.bone[2], fg.pivot[0].x - w.x, fg.pivot[0].y - w.y, fg.pivot[0].z - w.z,
                       fg.pivot[1].x - w.x, fg.pivot[1].y - w.y, fg.pivot[1].z - w.z, fg.pivot[2].x - w.x, fg.pivot[2].y - w.y,
                       fg.pivot[2].z - w.z, fg.axis.x, fg.axis.y, fg.axis.z);
                // The distal bone's skin centroid, posed open / bind / closed.
                double tw = 0;
                vm::Vec3 tip;
                for (const auto& v : verts)
                    for (int k = 0; k < 8; ++k)
                        if (v.bone[k] == fg.bone[2] && v.weight[k]) { tip = tip + v.pos * (v.weight[k] / 255.f); tw += v.weight[k] / 255.f; }
                tip = tip * float(1.0 / tw);
                mecvr::arm_ik::Rigid o[3], c[3];
                mecvr::finger_rig::pose(fg, 0, {}, o);
                mecvr::finger_rig::pose(fg, 1, {}, c);
                const vm::Vec3 po = o[2].apply(tip) - w, pc = c[2].apply(tip) - w, pb = tip - w;
                printf("         tip bind %+.3f %+.3f %+.3f  open %+.3f %+.3f %+.3f  closed %+.3f %+.3f %+.3f\n", pb.x, pb.y, pb.z, po.x,
                       po.y, po.z, pc.x, pc.y, pc.z);
                // The same knuckles from the bone matrices (joint fits over the recording), for comparison.
                const Fit j0 = joint(fr, res.arm[a].hand_bone, fg.bone[0]), j1 = joint(fr, fg.bone[0], fg.bone[1]),
                          j2 = joint(fr, fg.bone[1], fg.bone[2]);
                printf("         fits: MCP %+.3f %+.3f %+.3f (%.1f mm)  PIP %+.3f %+.3f %+.3f (%.1f mm)  DIP %+.3f %+.3f %+.3f (%.1f mm)\n",
                       j0.x.x - w.x, j0.x.y - w.y, j0.x.z - w.z, j0.rms * 1000, j1.x.x - w.x, j1.x.y - w.y, j1.x.z - w.z, j1.rms * 1000,
                       j2.x.x - w.x, j2.x.y - w.y, j2.x.z - w.z, j2.rms * 1000);
            }
        }
    }
    return 0;
}

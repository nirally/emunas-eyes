// Stage 7 recon: Faith's arm skeleton from recorded skinning matrices (bones_rec.bin, faith_bones BONEREC).
//
// Each recorded bone matrix S maps the bind pose to the world: S = M * B^-1 (rigid). A child's joint is a point
// that moves with its parent too, so for the right parent p there is one bind-space point x with
// S_child(x) = S_parent(x) in every frame: (R_c - R_p) x = t_p - t_c. Solved by least squares over all frames for
// every pair; each bone's parent is the one with the smallest residual (the joint must also be well determined,
// i.e. the two bones really rotate against each other). Prints the tree with joint positions (frame 0, world)
// and bone lengths.
//
// Usage: bone_solve <bones_rec.bin>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <array>
#include <vector>

struct M34 { double r[3][3], t[3]; };

static bool solve3(double a[3][3], const double b[3], double x[3], double* min_diag) {
    // Gaussian elimination with partial pivoting; min_diag = smallest pivot (conditioning check).
    double m[3][4];
    for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) m[i][j] = a[i][j]; m[i][3] = b[i]; }
    *min_diag = 1e300;
    for (int c = 0; c < 3; ++c) {
        int p = c;
        for (int r = c + 1; r < 3; ++r) if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
        for (int j = 0; j < 4; ++j) std::swap(m[c][j], m[p][j]);
        if (std::fabs(m[c][c]) < *min_diag) *min_diag = std::fabs(m[c][c]);
        if (std::fabs(m[c][c]) < 1e-12) return false;
        for (int r = 0; r < 3; ++r) if (r != c) {
            const double f = m[r][c] / m[c][c];
            for (int j = c; j < 4; ++j) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 3; ++i) x[i] = m[i][3] / m[i][i];
    return true;
}

static void apply(const M34& s, const double x[3], double out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = s.r[i][0] * x[0] + s.r[i][1] * x[1] + s.r[i][2] * x[2] + s.t[i];
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: bone_solve <bones_rec.bin>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<std::vector<M34>> frames;
    uint32_t n = 0;
    while (fread(&n, 4, 1, f) == 1 && n > 0 && n < 512) {
        std::vector<float> raw(size_t(n) * 12);
        if (fread(raw.data(), 4, raw.size(), f) != raw.size()) break;
        std::vector<M34> fr(n);
        for (uint32_t b = 0; b < n; ++b)
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) fr[b].r[i][j] = raw[b * 12 + i * 4 + j];
                fr[b].t[i] = raw[b * 12 + i * 4 + 3];
            }
        if (!frames.empty() && frames.back().size() != n) continue;
        frames.push_back(std::move(fr));
    }
    fclose(f);
    const size_t F = frames.size(), N = F ? frames[0].size() : 0;
    printf("%zu frames, %zu bones\n", F, N);
    if (F < 20) return 1;

    std::vector<int> parent(N, -1);
    std::vector<double> best_res(N, 1e9), best_cond(N, 0);
    std::vector<std::array<double, 3>> joint(N);
    for (size_t c = 0; c < N; ++c) {
        for (size_t p = 0; p < N; ++p) {
            if (p == c) continue;
            double ata[3][3] = {}, atb[3] = {};
            for (size_t k = 0; k < F; ++k) {
                const M34 &sc = frames[k][c], &sp = frames[k][p];
                for (int i = 0; i < 3; ++i) {
                    double a[3], b = sp.t[i] - sc.t[i];
                    for (int j = 0; j < 3; ++j) a[j] = sc.r[i][j] - sp.r[i][j];
                    for (int r = 0; r < 3; ++r) { atb[r] += a[r] * b; for (int s = 0; s < 3; ++s) ata[r][s] += a[r] * a[s]; }
                }
            }
            double x[3], cond;
            if (!solve3(ata, atb, x, &cond)) continue;
            cond /= double(F);  // per-frame pivot: how much the two bones rotate against each other
            if (cond < 1e-4) continue;
            double res = 0;
            for (size_t k = 0; k < F; ++k) {
                double a[3], b[3];
                apply(frames[k][c], x, a);
                apply(frames[k][p], x, b);
                res += (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
            }
            res = std::sqrt(res / double(F));
            if (res < best_res[c]) { best_res[c] = res; parent[c] = int(p); best_cond[c] = cond; joint[c] = {x[0], x[1], x[2]}; }
        }
        if (best_res[c] > 0.01) parent[c] = -1;  // nothing it's attached to within 1 cm: a root (or unsolvable)
    }
    // Joint positions in world, frame 0; bone length = distance to the parent's joint.
    printf("bone parent residual_mm cond  joint_world(frame0)            len_to_parent_joint_m  children\n");
    for (size_t c = 0; c < N; ++c) {
        double w[3] = {0, 0, 0};
        double x[3] = {joint[c][0], joint[c][1], joint[c][2]};
        if (parent[c] >= 0) apply(frames[0][c], x, w);
        double len = -1;
        if (parent[c] >= 0 && parent[parent[c]] >= 0) {
            double xp[3] = {joint[parent[c]][0], joint[parent[c]][1], joint[parent[c]][2]}, wp[3];
            apply(frames[0][parent[c]], xp, wp);
            len = std::sqrt((w[0] - wp[0]) * (w[0] - wp[0]) + (w[1] - wp[1]) * (w[1] - wp[1]) + (w[2] - wp[2]) * (w[2] - wp[2]));
        }
        printf("%3zu %4d %8.2f %7.4f  %9.3f %9.3f %9.3f  %7.3f   ", c, parent[c], best_res[c] * 1000, best_cond[c], w[0], w[1],
               w[2], len);
        for (size_t k = 0; k < N; ++k) if (parent[k] == int(c)) printf(" %zu", k);
        printf("\n");
    }
    return 0;
}

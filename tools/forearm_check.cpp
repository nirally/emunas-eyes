// Stage 10 recon (right-arm glove torn, 2026-10-28): per 400-frame window of a bones_rec.bin recording, the arm solve,
// and for each forearm bone how far it carries a point in her palm (on the forearm's line, 8 cm past the wrist) from
// where the main forearm bone and the hand bone carry it (arm_solve::Learner::apart, rms m). A twist bone turns about
// that line (small against the forearm); a bone riding with the hand is small against the hand.
//
// Usage: forearm_check <bones_rec.bin> [window frames]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/arm_solve.h"

using namespace mecvr;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: forearm_check <bones_rec.bin> [window]\n"); return 1; }
    const int window = argc > 2 ? atoi(argv[2]) : 400;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<std::vector<arm_solve::Bone>> frames;
    uint32_t n = 0;
    while (fread(&n, 4, 1, f) == 1 && n > 0 && n < 512) {
        std::vector<arm_solve::Bone> fr(n);
        if (fread(fr.data(), 48, n, f) != n) break;
        if (!frames.empty() && frames.back().size() != n) continue;
        frames.push_back(std::move(fr));
    }
    fclose(f);
    const int N = frames.empty() ? 0 : int(frames[0].size());
    printf("%zu frames, %d bones, windows of %d\n", frames.size(), N, window);
    for (size_t w0 = 0; w0 + window <= frames.size(); w0 += window) {
        arm_solve::Learner l;
        l.reset(N);
        for (size_t k = w0; k < w0 + window; ++k) l.add(frames[k].data());
        const auto r = l.solve();
        printf("window %zu: %s\n", w0 / window, r.why);
        if (!r.ok) continue;
        for (int a = 0; a < 2; ++a) {
            const auto& arm = r.arm[a];
            int main_fore = -1;  // touches elbow and wrist: the forearm bone apart() is smallest at both
            double best = 1e9;
            for (int b = 0; b < N; ++b)
                if (r.side[b] == a && r.segment[b] == arm_solve::kFore) {
                    const double d = l.apart(b, arm.elbow_bone, arm.elbow) + l.apart(b, arm.wrist_bone, arm.wrist);
                    if (d < best) { best = d; main_fore = b; }
                }
            const vm::Vec3 palm = arm.wrist + (arm.wrist - arm.elbow).normalized() * 0.08f;
            printf("  arm %d: hand bone %d, main forearm %d; forearm bones (vs forearm / vs hand, mm):", a, arm.hand_bone, main_fore);
            for (int b = 0; b < N; ++b)
                if (r.side[b] == a && r.segment[b] == arm_solve::kFore)
                    printf(" %d:%.0f/%.0f", b, main_fore >= 0 ? 1000 * l.apart(b, main_fore, palm) : -1.0,
                           arm.hand_bone >= 0 ? 1000 * l.apart(b, arm.hand_bone, palm) : -1.0);
            printf("\n");
        }
    }
    return 0;
}

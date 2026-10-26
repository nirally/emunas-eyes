#pragma once
// Stage 8A: the game's screen-space UI pulled in towards the middle of the frame. Pure, unit-tested.
//
// The game renders 129.5 x 100 deg (16:9) while a Quest 2 eye shows ~94 x 98 deg, so the outer ~18 deg each side
// never reaches the eye (Q-801: "the health bar is cropped and I can only see the end of it"; Q-802: menus "zoomed in
// to the center"). Each UI draw's viewport and scissor rectangle are scaled by k about the frame's centre.

namespace mecvr::ui_fit {

struct Rect {
    float x, y, w, h;
};

// r scaled by k about the centre of a frame of size fw x fh (k = 1 leaves it).
inline Rect scale_about_centre(Rect r, float fw, float fh, float k) {
    const float cx = fw * 0.5f, cy = fh * 0.5f;
    return {cx + (r.x - cx) * k, cy + (r.y - cy) * k, r.w * k, r.h * k};
}

// r moved by the frame-space map f' = k f + o (f: -1..1 across the frame, y up), in pixels of a fw x fh frame. With
// o = 0 it is scale_about_centre. 8C: the health bar's corner placed on its own (ui_shrink).
inline Rect place(Rect r, float fw, float fh, float k, float ox, float oy) {
    return {k * r.x + fw * 0.5f * (1.f - k + ox), k * r.y + fh * 0.5f * (1.f - k - oy), r.w * k, r.h * k};
}

}  // namespace mecvr::ui_fit

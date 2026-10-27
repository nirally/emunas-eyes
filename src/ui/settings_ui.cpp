// Stage 9: the in-VR settings menu. See settings_ui.h.
#include "ui/settings_ui.h"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/settings_menu.h"
#include "diagnostics/log.h"

namespace mecvr::settings_ui {
namespace {

using json = nlohmann::json;
using settings_menu::Key;

std::mutex g_mutex;
std::unique_ptr<settings_menu::Menu> g_menu;
json g_values = json::object();   // shown values (a change shows at once, before the main thread writes it)
json g_pending = json::object();  // changes for the main thread
std::atomic<bool> g_open{false};
bool g_dirty = true;  // the panel needs redrawing (render thread, under g_mutex)
std::atomic<bool> g_flat{false};

// Stick repeat: a key on the push, then again after kFirst s and every kRepeat s while held.
constexpr double kFirst = 0.40, kRepeat = 0.11;
constexpr float kOn = 0.65f, kOff = 0.35f;
int g_dir = 0;  // 0 none, 1 up, 2 down, 3 left, 4 right
double g_next = 0;
bool g_ok_was = true, g_back_was = true;  // wait for a release after opening (the hold that opened it)

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Under g_mutex.
void press(Key k) {
    if (!g_menu) return;
    bool close = false;
    for (const auto& c : g_menu->press(k, g_values, &close)) {
        g_values[c.key] = c.value;
        g_pending[c.key] = c.value;
        LOG_I("menu", "%s = %s", c.key.c_str(), c.value.dump().c_str());
    }
    g_dirty = true;
    if (close) {
        g_open = false;
        LOG_I("menu", "settings menu closed");
    }
}

// --- the panel ---------------------------------------------------------------
struct Gdi {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    uint32_t* px = nullptr;
    HFONT title = nullptr, row = nullptr, hint = nullptr;
};
Gdi g_gdi;
ID3D11Texture2D* g_tex = nullptr;
std::vector<uint32_t> g_rgba;

bool ensure_gdi() {
    if (g_gdi.dc) return true;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kWidth;
    bi.bmiHeader.biHeight = -kHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_gdi.dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    g_gdi.bmp = g_gdi.dc ? CreateDIBSection(g_gdi.dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!g_gdi.bmp) {
        LOG_E("menu", "could not create the panel bitmap");
        return false;
    }
    g_gdi.px = static_cast<uint32_t*>(bits);
    SelectObject(g_gdi.dc, g_gdi.bmp);
    auto font = [](int h, int weight) {
        return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    g_gdi.title = font(48, FW_BOLD);
    g_gdi.row = font(34, FW_SEMIBOLD);
    g_gdi.hint = font(28, FW_NORMAL);
    SetBkMode(g_gdi.dc, TRANSPARENT);
    return true;
}

void fill(RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(g_gdi.dc, &r, b);
    DeleteObject(b);
}

void text(const std::string& s, RECT r, HFONT f, COLORREF c, UINT flags) {
    SelectObject(g_gdi.dc, f);
    SetTextColor(g_gdi.dc, c);
    std::wstring w = wide(s);
    DrawTextW(g_gdi.dc, w.c_str(), int(w.size()), &r, flags | DT_NOPREFIX);
}

// Under g_mutex.
void draw() {
    constexpr COLORREF kBack = RGB(22, 24, 30), kRow = RGB(236, 238, 242), kDim = RGB(150, 156, 168), kSel = RGB(196, 32, 38),
                       kValue = RGB(255, 214, 120);
    fill({0, 0, kWidth, kHeight}, kBack);
    fill({0, 0, kWidth, 8}, kSel);
    text(g_menu->title(), {40, 24, kWidth - 40, 90}, g_gdi.title, kRow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    fill({40, 100, kWidth - 40, 102}, kDim);
    const std::vector<settings_menu::Line> lines = g_menu->lines(g_values);
    constexpr int kTop = 116, kRowH = 54, kRows = 13;
    const int cur = g_menu->cursor(), n = int(lines.size());
    const int first = std::max(0, std::min(cur - kRows / 2, n - kRows));
    for (int i = first; i < std::min(n, first + kRows); ++i) {
        const int y = kTop + (i - first) * kRowH;
        const auto& l = lines[i];
        if (l.selected) fill({28, y, kWidth - 28, y + kRowH - 6}, kSel);
        const bool value = !l.value.empty();
        text(l.text, {44, y, value ? kWidth - 300 : kWidth - 44, y + kRowH - 6}, g_gdi.row, kRow,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (value)
            text(l.value, {kWidth - 290, y, kWidth - 44, y + kRowH - 6}, g_gdi.row, l.selected ? RGB(255, 255, 255) : kValue,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    if (first > 0) text("...", {kWidth - 80, kTop - 30, kWidth - 44, kTop}, g_gdi.hint, kDim, DT_RIGHT | DT_SINGLELINE);
    if (first + kRows < n)
        text("...", {kWidth - 80, kTop + kRows * kRowH - 10, kWidth - 44, kTop + kRows * kRowH + 20}, g_gdi.hint, kDim, DT_RIGHT | DT_SINGLELINE);
    // The selected row in full (labels are cut to fit), then what the keys do.
    const int y0 = kTop + kRows * kRowH + 20;
    fill({40, y0 - 8, kWidth - 40, y0 - 6}, kDim);
    if (cur < n) text(lines[cur].text, {44, y0, kWidth - 44, kHeight - 64}, g_gdi.hint, kRow, DT_LEFT | DT_WORDBREAK);
    text(g_menu->help(g_values) + "   Hold both sticks: close", {44, kHeight - 56, kWidth - 44, kHeight - 16}, g_gdi.hint, kDim,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    GdiFlush();
    // GDI's BGRX -> RGBA, opaque.
    g_rgba.resize(size_t(kWidth) * kHeight);
    for (size_t i = 0; i < g_rgba.size(); ++i) {
        const uint32_t p = g_gdi.px[i];
        g_rgba[i] = 0xff000000u | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
    }
}

}  // namespace

void init(const nlohmann::ordered_json& schema) {
    std::lock_guard lock(g_mutex);
    if (!schema.is_object()) {
        LOG_E("menu", "schema.json didn't parse - no settings menu");
        return;
    }
    auto items = settings_menu::items_from_schema(schema);
    LOG_I("menu", "settings menu: %zu settings from the schema (click and hold both sticks 1 s to open)", items.size());
    g_menu = std::make_unique<settings_menu::Menu>(std::move(items));
    g_dirty = true;
}

void set_values(const json& values) {
    std::lock_guard lock(g_mutex);
    g_values = values;
    for (auto& [k, v] : g_pending.items()) g_values[k] = v;  // not written yet: keep showing ours
    g_dirty = true;
}

json take_changes() {
    std::lock_guard lock(g_mutex);
    json out = std::move(g_pending);
    g_pending = json::object();
    return out;
}

bool is_open() { return g_open.load(); }

void toggle() {
    std::lock_guard lock(g_mutex);
    if (!g_menu) return;
    const bool open = !g_open.load();
    if (open) {
        g_menu->reset_view();
        g_ok_was = g_back_was = true;
        g_dir = 0;
    }
    g_open = open;
    g_dirty = true;
    LOG_I("menu", "settings menu %s", open ? "opened" : "closed");
}

bool input(const Controls& c, double now_s) {
    std::lock_guard lock(g_mutex);
    if (!g_open.load() || !g_menu) return false;
    bool fired = false;
    // The stick: the bigger axis, with hysteresis.
    const float ax = std::fabs(c.x), ay = std::fabs(c.y);
    int dir = 0;
    const float on = g_dir ? kOff : kOn;
    if (std::max(ax, ay) >= on) dir = ay >= ax ? (c.y > 0 ? 1 : 2) : (c.x < 0 ? 3 : 4);
    if (dir && (dir != g_dir || now_s >= g_next)) {
        g_next = now_s + (dir != g_dir ? kFirst : kRepeat);
        press(dir == 1 ? Key::Up : dir == 2 ? Key::Down : dir == 3 ? Key::Left : Key::Right);
        fired = true;
    }
    g_dir = dir;
    if (c.ok && !g_ok_was) { press(Key::Ok); fired = true; }
    if (c.back && !g_back_was) { press(Key::Back); fired = true; }
    g_ok_was = c.ok;
    g_back_was = c.back;
    return fired;
}

ID3D11Texture2D* texture(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    std::lock_guard lock(g_mutex);
    if (!g_menu || !ensure_gdi()) return nullptr;
    if (!g_tex) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = kWidth;
        d.Height = kHeight;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc = {1, 0};
        d.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_tex))) {
            LOG_E("menu", "could not create the panel texture");
            return nullptr;
        }
        g_dirty = true;
    }
    if (g_dirty) {
        draw();
        ctx->UpdateSubresource(g_tex, 0, nullptr, g_rgba.data(), kWidth * 4, 0);
        g_dirty = false;
    }
    return g_tex;
}

void set_flat(bool on) { g_flat = on; }
bool flat() { return g_flat.load(); }

void on_present_flat(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* back) {
    if (!g_flat.load()) return;
    static bool was[5] = {};
    const int vk[5] = {VK_HOME, VK_UP, VK_DOWN, VK_RETURN, VK_BACK};
    bool now[5];
    for (int i = 0; i < 5; ++i) now[i] = (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
    if (now[0] && !was[0]) toggle();
    if (is_open()) {
        std::lock_guard lock(g_mutex);
        if (now[1] && !was[1]) press(Key::Up);
        if (now[2] && !was[2]) press(Key::Down);
        if (now[3] && !was[3]) press(Key::Ok);
        if (now[4] && !was[4]) press(Key::Back);
    }
    static bool lr[2] = {};
    if (is_open()) {  // left/right through the same path as the sticks' (Left/Right arrows)
        const bool l = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
        std::lock_guard lock(g_mutex);
        if (l && !lr[0]) press(Key::Left);
        if (r && !lr[1]) press(Key::Right);
        lr[0] = l;
        lr[1] = r;
    }
    for (int i = 0; i < 5; ++i) was[i] = now[i];
    if (!is_open() || !back) return;
    ID3D11Texture2D* t = texture(dev, ctx);
    D3D11_TEXTURE2D_DESC bd;
    back->GetDesc(&bd);
    if (!t || bd.Width < UINT(kWidth) || bd.Height < UINT(kHeight)) return;
    if (bd.Format != DXGI_FORMAT_R8G8B8A8_UNORM && bd.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        bd.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS)
        return;
    ctx->CopySubresourceRegion(back, 0, 0, 0, 0, t, 0, nullptr);
}

}  // namespace mecvr::settings_ui

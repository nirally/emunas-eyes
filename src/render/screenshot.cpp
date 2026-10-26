// Developer screenshots, triggered by files in the mod folder:
//   SHOT - the next presented back buffer is saved as shot_<n>.bmp.
//   PAIR - the next two presented frames (one left-eye and one right-eye frame in stereo) are saved
//          half size as pair_<n>_a.bmp / _b.bmp, plus pair_<n>_diff.bmp (white where they differ), and
//          the difference is logged. With diagnostics.zero_ipd both eyes share one viewpoint, so any
//          difference is something that changed between the two eyes (eye-sync checks, D-021).
#include "render/screenshot.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::screenshot {
namespace {

struct Image {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgb;  // top-down, 3 bytes per pixel
};

// Copies the back buffer to CPU memory, keeping every `step`-th pixel in both directions.
bool grab(IDXGISwapChain* chain, uint32_t step, Image& out) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
    ID3D11Device* dev = nullptr;
    back->GetDevice(&dev);
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    bool ok = false;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
        ctx->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            out.w = d.Width / step;
            out.h = d.Height / step;
            out.rgb.resize(size_t(out.w) * out.h * 3);
            for (uint32_t y = 0; y < out.h; ++y) {
                const uint8_t* src = static_cast<const uint8_t*>(m.pData) + size_t(y) * step * m.RowPitch;
                uint8_t* dst = out.rgb.data() + size_t(y) * out.w * 3;
                for (uint32_t x = 0; x < out.w; ++x) {
                    const uint8_t* p = src + size_t(x) * step * 4;
                    dst[x * 3 + 0] = bgra ? p[2] : p[0];
                    dst[x * 3 + 1] = p[1];
                    dst[x * 3 + 2] = bgra ? p[0] : p[2];
                }
            }
            ctx->Unmap(staging, 0);
            ok = true;
        }
        staging->Release();
    }
    ctx->Release();
    dev->Release();
    back->Release();
    return ok;
}

void save_bmp(const std::filesystem::path& file, const Image& img) {
    const uint32_t row = (img.w * 3 + 3) & ~3u;
    std::vector<uint8_t> px(size_t(row) * img.h);
    for (uint32_t y = 0; y < img.h; ++y) {
        const uint8_t* src = img.rgb.data() + size_t(y) * img.w * 3;
        uint8_t* dst = px.data() + size_t(img.h - 1 - y) * row;  // BMP is bottom-up, BGR
        for (uint32_t x = 0; x < img.w; ++x) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f) return;
    BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(54 + px.size()), 0, 0, 54};
    BITMAPINFOHEADER ih{sizeof(ih), static_cast<LONG>(img.w), static_cast<LONG>(img.h), 1, 24};
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
}

void write_pair(const std::filesystem::path& dir, int index, const Image& a, const Image& b) {
    wchar_t name[48];
    swprintf_s(name, L"pair_%d_a.bmp", index);
    save_bmp(dir / name, a);
    swprintf_s(name, L"pair_%d_b.bmp", index);
    save_bmp(dir / name, b);
    if (a.w != b.w || a.h != b.h) { LOG_W("shot", "pair %d: frame sizes differ", index); return; }
    Image diff{a.w, a.h, std::vector<uint8_t>(a.rgb.size(), 0)};
    uint64_t changed = 0, sum = 0;
    uint32_t x0 = a.w, y0 = a.h, x1 = 0, y1 = 0;
    for (uint32_t y = 0; y < a.h; ++y)
        for (uint32_t x = 0; x < a.w; ++x) {
            const size_t i = (size_t(y) * a.w + x) * 3;
            int dmax = 0;
            for (int c = 0; c < 3; ++c) dmax = std::max(dmax, std::abs(int(a.rgb[i + c]) - int(b.rgb[i + c])));
            sum += dmax;
            if (dmax > 24) {
                ++changed;
                diff.rgb[i] = diff.rgb[i + 1] = diff.rgb[i + 2] = 255;
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
        }
    // Sharpness: mean absolute luminance gradient (x and y), per frame. Softening from AA history
    // that comes from the other eye shows up as a lower number than mono.
    auto sharpness = [](const Image& im) {
        auto lum = [&im](uint32_t x, uint32_t y) {
            const uint8_t* p = im.rgb.data() + (size_t(y) * im.w + x) * 3;
            return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
        };
        double s = 0;
        for (uint32_t y = 0; y + 1 < im.h; ++y)
            for (uint32_t x = 0; x + 1 < im.w; ++x) {
                const double c = lum(x, y);
                s += std::abs(lum(x + 1, y) - c) + std::abs(lum(x, y + 1) - c);
            }
        return s / (double(im.w - 1) * (im.h - 1));
    };
    LOG_I("shot", "pair %d: sharpness a %.3f b %.3f", index, sharpness(a), sharpness(b));
    swprintf_s(name, L"pair_%d_diff.bmp", index);
    save_bmp(dir / name, diff);
    const double total = double(a.w) * a.h;
    if (changed)
        LOG_I("shot", "pair %d: %.2f%% of pixels differ (mean %.2f), within x %u..%u y %u..%u of %ux%u", index,
              100.0 * changed / total, sum / total, x0, x1, y0, y1, a.w, a.h);
    else
        LOG_I("shot", "pair %d: frames identical (mean diff %.2f) at %ux%u", index, sum / total, a.w, a.h);
}

std::atomic<bool> g_request{false};

}  // namespace

void request() { g_request = true; }

void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir) {
    // A PAIR request grabs 4 consecutive frames and diffs each neighbouring two (3 results), so
    // a left->right and a right->left step are both seen whatever eye the capture starts on, then
    // each frame against the one two later (2 results: the same eye in stereo).
    constexpr int kFrames = 4;
    static int poll = 0, shot_index = 0, pair_index = 0, pair_stage = 0;
    static Image frames[kFrames];
    if (pair_stage > 0) {
        if (!grab(chain, 2, frames[pair_stage - 1])) { pair_stage = 0; return; }
        if (++pair_stage > kFrames) {
            for (int i = 0; i + 1 < kFrames; ++i) write_pair(dir, ++pair_index, frames[i], frames[i + 1]);
            // Same eye, two frames apart: shimmer / temporal stability of each eye on its own.
            for (int i = 0; i + 2 < kFrames; ++i) {
                LOG_I("shot", "pair %d is same-eye (frames %d and %d)", pair_index + 1, i + 1, i + 3);
                write_pair(dir, ++pair_index, frames[i], frames[i + 2]);
            }
            pair_stage = 0;
        }
        return;
    }
    if (g_request.exchange(false)) {
        Image img;
        if (!grab(chain, 1, img)) return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t name[48];
        swprintf_s(name, L"vrshot_%04u%02u%02u_%02u%02u%02u.bmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        save_bmp(dir / name, img);
        LOG_I("shot", "headset screenshot saved: %ls (%ux%u)", name, img.w, img.h);
        return;
    }
    if (++poll % 30) return;
    std::error_code ec;
    if (std::filesystem::remove(dir / L"PAIR", ec)) { pair_stage = 1; return; }
    if (!std::filesystem::remove(dir / L"SHOT", ec)) return;
    Image img;
    if (!grab(chain, 1, img)) return;
    wchar_t name[32];
    swprintf_s(name, L"shot_%d.bmp", ++shot_index);
    save_bmp(dir / name, img);
    LOG_I("shot", "saved %ls (%ux%u)", name, img.w, img.h);
}

}  // namespace mecvr::screenshot

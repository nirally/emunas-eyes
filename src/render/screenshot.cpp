// Developer screenshot: when a SHOT file appears in the mod folder, the next presented
// back buffer is saved as shot_<n>.bmp, so results can be checked from the image.
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::screenshot {

void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir) {
    static int poll = 0, index = 0;
    if (++poll % 30) return;
    std::error_code ec;
    if (!std::filesystem::remove(dir / L"SHOT", ec)) return;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
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
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
        ctx->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            // RGBA8 -> 24-bit BMP, bottom-up.
            const uint32_t w = d.Width, h = d.Height, row = (w * 3 + 3) & ~3u;
            std::vector<uint8_t> px(row * h);
            bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t* src = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch;
                uint8_t* dst = px.data() + (h - 1 - y) * row;
                for (uint32_t x = 0; x < w; ++x) {
                    dst[x * 3 + 0] = bgra ? src[x * 4 + 0] : src[x * 4 + 2];
                    dst[x * 3 + 1] = src[x * 4 + 1];
                    dst[x * 3 + 2] = bgra ? src[x * 4 + 2] : src[x * 4 + 0];
                }
            }
            ctx->Unmap(staging, 0);
            wchar_t name[32];
            swprintf_s(name, L"shot_%d.bmp", ++index);
            FILE* f = nullptr;
            if (_wfopen_s(&f, (dir / name).c_str(), L"wb") == 0 && f) {
                BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(54 + px.size()), 0, 0, 54};
                BITMAPINFOHEADER ih{sizeof(ih), static_cast<LONG>(w), static_cast<LONG>(h), 1, 24};
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                fwrite(px.data(), 1, px.size(), f);
                fclose(f);
                LOG_I("shot", "saved %ls (%ux%u, format %d)", name, w, h, d.Format);
            }
        }
        staging->Release();
    }
    ctx->Release();
    dev->Release();
    back->Release();
}

}  // namespace mecvr::screenshot

#include "render/present_hook.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <atomic>

#include <MinHook.h>

#include "diagnostics/log.h"

namespace mecvr::present_hook {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_present_orig = nullptr;
ResizeFn g_resize_orig = nullptr;
void* g_present_target = nullptr;
void* g_resize_target = nullptr;

std::atomic<uint64_t> g_frames{0};
uint64_t g_window_frames = 0;
ULONGLONG g_window_start = 0;
IDXGISwapChain* g_last_chain = nullptr;

void describe(IDXGISwapChain* chain, const char* why) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(chain->GetDesc(&d))) return;
    ID3D11Device* dev = nullptr;
    bool d3d11 = SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev)));
    LOG_I("render", "%s: swapchain %p %ux%u fmt=%d buffers=%u windowed=%d device=%s", why, chain,
          d.BufferDesc.Width, d.BufferDesc.Height, d.BufferDesc.Format, d.BufferCount, d.Windowed,
          d3d11 ? "D3D11" : "not D3D11");
    if (dev) dev->Release();
}

HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* chain, UINT sync, UINT flags) {
    uint64_t n = ++g_frames;
    if (chain != g_last_chain) {
        g_last_chain = chain;
        describe(chain, n == 1 ? "Present hook live" : "swapchain changed");
    }
    ULONGLONG now = GetTickCount64();
    if (g_window_start == 0) g_window_start = now;
    ++g_window_frames;
    if (now - g_window_start >= 10000) {
        LOG_I("render", "fps %.1f (frame %llu)", g_window_frames * 1000.0 / (now - g_window_start),
              static_cast<unsigned long long>(n));
        g_window_start = now;
        g_window_frames = 0;
    }
    return g_present_orig(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE resize_detour(IDXGISwapChain* chain, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                                        UINT flags) {
    LOG_I("render", "ResizeBuffers %ux%u", w, h);
    return g_resize_orig(chain, count, w, h, fmt, flags);
}

}  // namespace

bool install() {
    // Throwaway hidden window + device, only to read the swapchain vtable.
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"mecvr_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr,
                                wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    IDXGISwapChain* chain = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    // Resolve at runtime: a static import would force d3d11 to load the moment the game loads us.
    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
        GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "D3D11CreateDeviceAndSwapChain"));
    if (!create) {
        LOG_E("render", "d3d11.dll not loaded by the game yet");
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &chain, &dev, nullptr, &ctx);
    if (FAILED(hr)) {
        LOG_E("render", "dummy device failed hr=0x%08lx", hr);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }
    void** vtbl = *reinterpret_cast<void***>(chain);
    g_present_target = vtbl[8];
    g_resize_target = vtbl[13];
    chain->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_E("render", "MinHook init failed: %s", MH_StatusToString(st));
        return false;
    }
    st = MH_CreateHook(g_present_target, reinterpret_cast<void*>(&present_detour),
                       reinterpret_cast<void**>(&g_present_orig));
    if (st != MH_OK) {
        LOG_E("render", "Present hook create failed: %s", MH_StatusToString(st));
        return false;
    }
    st = MH_CreateHook(g_resize_target, reinterpret_cast<void*>(&resize_detour),
                       reinterpret_cast<void**>(&g_resize_orig));
    if (st != MH_OK) LOG_W("render", "ResizeBuffers hook create failed: %s", MH_StatusToString(st));

    st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK) {
        LOG_E("render", "enable hooks failed: %s", MH_StatusToString(st));
        return false;
    }
    LOG_I("render", "Present hook installed at %p (waiting for first frame)", g_present_target);
    return true;
}

void uninstall() {
    if (g_present_target) MH_DisableHook(g_present_target);
    if (g_resize_target) MH_DisableHook(g_resize_target);
}

uint64_t frame_count() { return g_frames.load(); }

}  // namespace mecvr::present_hook

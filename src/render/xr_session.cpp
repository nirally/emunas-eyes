#include "render/xr_session.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "core/head_pose.h"
#include "diagnostics/log.h"

namespace mecvr::xr_session {
namespace {

std::filesystem::path g_dir;
bool g_enabled = false;
bool g_tried = false;      // one init attempt per launch
bool g_failed = false;
std::atomic<bool> g_running{false};
std::atomic<float> g_sx{1.0806f}, g_sy{1.9210f};

// Loader + function table (everything resolved through xrGetInstanceProcAddr).
HMODULE g_loader = nullptr;
PFN_xrGetInstanceProcAddr g_gipa = nullptr;
#define XR_FN(name) PFN_##name p##name = nullptr;
#define XR_FUNCS(X) \
    X(xrCreateInstance) X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrGetSystem) X(xrCreateSession) \
    X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrPollEvent) X(xrCreateReferenceSpace) \
    X(xrLocateSpace) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrEnumerateSwapchainFormats) \
    X(xrCreateSwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) X(xrGetD3D11GraphicsRequirementsKHR) X(xrResultToString) \
    X(xrEnumerateInstanceExtensionProperties) X(xrRequestExitSession) X(xrGetSystemProperties)
XR_FUNCS(XR_FN)

XrInstance g_instance = XR_NULL_HANDLE;
XrSystemId g_system = XR_NULL_SYSTEM_ID;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_local = XR_NULL_HANDLE, g_view = XR_NULL_HANDLE;
XrSwapchain g_swapchain = XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> g_images;
int64_t g_format = 0;
uint32_t g_width = 0, g_height = 0;
XrSessionState g_state = XR_SESSION_STATE_UNKNOWN;
bool g_session_running = false;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

const char* rstr(XrResult r) {
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (pxrResultToString && g_instance && XR_SUCCEEDED(pxrResultToString(g_instance, r, buf))) return buf;
    snprintf(buf, sizeof(buf), "XrResult %d", r);
    return buf;
}

#define XR_CHECK(call, what)                                                   \
    do {                                                                       \
        XrResult r_ = (call);                                                  \
        if (XR_FAILED(r_)) {                                                   \
            LOG_E("xr", "%s failed: %s", what, rstr(r_));                      \
            return false;                                                      \
        }                                                                      \
    } while (0)

bool load_loader() {
    auto path = g_dir / L"openxr_loader.dll";
    g_loader = LoadLibraryW(path.c_str());
    if (!g_loader) {
        LOG_E("xr", "openxr_loader.dll not found in mecvr\\ (error %lu)", GetLastError());
        return false;
    }
    g_gipa = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(g_loader, "xrGetInstanceProcAddr"));
    if (!g_gipa) return false;
    g_gipa(XR_NULL_HANDLE, "xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction*>(&pxrCreateInstance));
    g_gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
           reinterpret_cast<PFN_xrVoidFunction*>(&pxrEnumerateInstanceExtensionProperties));
    return pxrCreateInstance && pxrEnumerateInstanceExtensionProperties;
}

bool resolve_all() {
#define XR_RESOLVE(name) \
    if (XR_FAILED(g_gipa(g_instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&p##name))) || !p##name) { \
        LOG_E("xr", "missing function %s", #name); return false; }
    XR_FUNCS(XR_RESOLVE)
#undef XR_RESOLVE
    return true;
}

bool create_instance() {
    uint32_t n = 0;
    pxrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
    pxrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
    bool has_d3d11 = false;
    for (auto& e : exts) if (!strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) has_d3d11 = true;
    if (!has_d3d11) {
        LOG_E("xr", "runtime has no D3D11 support (XR_KHR_D3D11_enable)");
        return false;
    }
    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "MEC VR");
    ci.applicationInfo.applicationVersion = 1;
    strcpy_s(ci.applicationInfo.engineName, "Frostbite (MEC VR mod)");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = enabled;
    XrResult r = pxrCreateInstance(&ci, &g_instance);
    if (XR_FAILED(r)) {
        LOG_E("xr", "xrCreateInstance failed (%d) - is SteamVR installed and set as the OpenXR runtime?", r);
        return false;
    }
    if (!resolve_all()) return false;
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    pxrGetInstanceProperties(g_instance, &props);
    LOG_I("xr", "OpenXR runtime: %s %u.%u.%u", props.runtimeName, XR_VERSION_MAJOR(props.runtimeVersion),
          XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));
    return true;
}

bool create_session() {
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult r = pxrGetSystem(g_instance, &si, &g_system);
    if (XR_FAILED(r)) {
        LOG_E("xr", "no headset found (%s) - is it connected through Steam Link?", rstr(r));
        return false;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    pxrGetSystemProperties(g_instance, g_system, &sp);
    LOG_I("xr", "headset: %s", sp.systemName);

    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    XR_CHECK(pxrGetD3D11GraphicsRequirementsKHR(g_instance, g_system, &req), "graphics requirements");
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) {
        if (SUCCEEDED(dxgi->GetAdapter(&adapter))) { adapter->GetDesc(&ad); adapter->Release(); }
        dxgi->Release();
    }
    if (memcmp(&ad.AdapterLuid, &req.adapterLuid, sizeof(LUID)) != 0) {
        LOG_E("xr", "the game renders on a different GPU than the headset needs - cannot share frames");
        return false;
    }

    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = g_device;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding;
    ci.systemId = g_system;
    XR_CHECK(pxrCreateSession(g_instance, &ci, &g_session), "xrCreateSession");

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XR_CHECK(pxrCreateReferenceSpace(g_session, &rs, &g_local), "LOCAL space");
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    XR_CHECK(pxrCreateReferenceSpace(g_session, &rs, &g_view), "VIEW space");
    return true;
}

bool create_swapchain(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC d{};
    chain->GetDesc(&d);
    g_width = d.BufferDesc.Width;
    g_height = d.BufferDesc.Height;

    uint32_t n = 0;
    pxrEnumerateSwapchainFormats(g_session, 0, &n, nullptr);
    std::vector<int64_t> formats(n);
    pxrEnumerateSwapchainFormats(g_session, n, &n, formats.data());
    // The game's back buffer holds gamma-encoded RGBA8; an sRGB swapchain tells the
    // runtime so (a plain UNORM one would be treated as linear and look washed out).
    for (int64_t f : formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) g_format = f;
    if (!g_format) {
        LOG_E("xr", "runtime offers no R8G8B8A8_UNORM_SRGB swapchain");
        return false;
    }
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = g_format;
    ci.sampleCount = 1;
    ci.width = g_width;
    ci.height = g_height;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XR_CHECK(pxrCreateSwapchain(g_session, &ci, &g_swapchain), "xrCreateSwapchain");
    pxrEnumerateSwapchainImages(g_swapchain, 0, &n, nullptr);
    g_images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    XR_CHECK(pxrEnumerateSwapchainImages(g_swapchain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(g_images.data())),
             "swapchain images");
    LOG_I("xr", "mono swapchain %ux%u, %u images", g_width, g_height, n);
    return true;
}

bool init(IDXGISwapChain* chain) {
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) return false;
    g_device->GetImmediateContext(&g_ctx);
    return load_loader() && create_instance() && create_session() && create_swapchain(chain);
}

void poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (pxrPollEvent(g_instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            g_state = s->state;
            LOG_I("xr", "session state %d", g_state);
            if (g_state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(pxrBeginSession(g_session, &bi))) {
                    g_session_running = true;
                    head_pose::request_recenter();
                    LOG_I("xr", "session running - headset drives the camera");
                }
            } else if (g_state == XR_SESSION_STATE_STOPPING) {
                pxrEndSession(g_session);
                g_session_running = false;
                head_pose::set_headset_pose({}, false);
            } else if (g_state == XR_SESSION_STATE_EXITING || g_state == XR_SESSION_STATE_LOSS_PENDING) {
                g_session_running = false;
                g_failed = true;  // don't retry this launch
                head_pose::set_headset_pose({}, false);
                LOG_W("xr", "session ended by the runtime - continuing flat");
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

vm::Quat to_quat(const XrQuaternionf& q) { return {q.x, q.y, q.z, q.w}; }
XrQuaternionf to_xr(vm::Quat q) { return {q.x, q.y, q.z, q.w}; }

void frame(IDXGISwapChain* chain) {
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(pxrWaitFrame(g_session, &wi, &fs))) return;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(pxrBeginFrame(g_session, &bi))) return;

    // Head pose for upcoming game frames.
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    bool pose_ok = XR_SUCCEEDED(pxrLocateSpace(g_view, g_local, fs.predictedDisplayTime, &loc)) &&
                   (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
                   (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
    if (pose_ok) {
        head_pose::Pose p;
        p.rotation = to_quat(loc.pose.orientation);
        p.position = {loc.pose.position.x, loc.pose.position.y, loc.pose.position.z};
        p.valid = true;
        head_pose::set_headset_pose(p, true);
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView views[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                 {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    uint32_t layer_count = 0;

    if (fs.shouldRender) {
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wsi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wsi.timeout = XR_INFINITE_DURATION;
        if (XR_SUCCEEDED(pxrAcquireSwapchainImage(g_swapchain, &ai, &idx)) &&
            XR_SUCCEEDED(pxrWaitSwapchainImage(g_swapchain, &wsi))) {
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
                g_ctx->CopySubresourceRegion(g_images[idx].texture, 0, 0, 0, 0, back, 0, nullptr);
                back->Release();
            }
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            pxrReleaseSwapchainImage(g_swapchain, &ri);

            // The frame on screen was rendered with the head pose the game camera used.
            head_pose::Pose used = head_pose::rendered_pose();
            float half_x = std::atan(1.f / g_sx.load()), half_y = std::atan(1.f / g_sy.load());
            for (int e = 0; e < 2; ++e) {
                views[e].pose.orientation = to_xr(used.rotation);
                views[e].pose.position = {used.position.x, used.position.y, used.position.z};
                views[e].fov = {-half_x, half_x, half_y, -half_y};
                views[e].subImage.swapchain = g_swapchain;
                views[e].subImage.imageRect = {{0, 0}, {static_cast<int32_t>(g_width), static_cast<int32_t>(g_height)}};
            }
            layer.space = g_local;
            layer.viewCount = 2;
            layer.views = views;
            layer_count = 1;
        }
    }

    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layer_count;
    ei.layers = layers;
    XrResult r = pxrEndFrame(g_session, &ei);
    static int end_errors = 0;
    if (XR_FAILED(r) && end_errors++ < 5) LOG_E("xr", "xrEndFrame: %s", rstr(r));
}

}  // namespace

void configure(const std::filesystem::path& mod_dir, bool enabled) {
    g_dir = mod_dir;
    g_enabled = enabled;
}

void set_game_projection(float sx, float sy) {
    if (sx > 0.1f && sy > 0.1f) { g_sx = sx; g_sy = sy; }
}

bool running() { return g_running.load(); }

void on_present(IDXGISwapChain* chain) {
    if (!g_enabled || g_failed) return;
    if (!g_tried) {
        g_tried = true;
        if (!init(chain)) {
            g_failed = true;
            LOG_W("xr", "OpenXR unavailable - staying flat (desktop simulator still works)");
            return;
        }
    }
    poll_events();
    g_running = g_session_running;
    if (g_session_running) frame(chain);
}

}  // namespace mecvr::xr_session

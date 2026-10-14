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
#include "input/xr_input.h"

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
    X(xrEnumerateInstanceExtensionProperties) X(xrRequestExitSession) X(xrGetSystemProperties) X(xrLocateViews) \
    X(xrDestroySwapchain)
XR_FUNCS(XR_FN)

XrInstance g_instance = XR_NULL_HANDLE;
XrSystemId g_system = XR_NULL_SYSTEM_ID;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_local = XR_NULL_HANDLE, g_view = XR_NULL_HANDLE, g_stage = XR_NULL_HANDLE;
XrSwapchain g_swapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};  // [eye]; mono uses [0] for both eyes
std::vector<XrSwapchainImageD3D11KHR> g_images[2];
bool g_has_image[2] = {false, false};
head_pose::Pose g_eye_pose[2];       // pose each eye's latest image was rendered with
ID3D11Texture2D* g_hold = nullptr;   // left eye image waiting for its right eye
head_pose::Pose g_hold_pose;
bool g_pending_left = false;
uint64_t g_pairs = 0, g_orphans = 0;
bool g_stereo_mode = false;
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
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;  // floor level; only used to report eye height
    if (XR_FAILED(pxrCreateReferenceSpace(g_session, &rs, &g_stage))) g_stage = XR_NULL_HANDLE;
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
    for (int e = 0; e < 2; ++e) {
        XR_CHECK(pxrCreateSwapchain(g_session, &ci, &g_swapchain[e]), "xrCreateSwapchain");
        pxrEnumerateSwapchainImages(g_swapchain[e], 0, &n, nullptr);
        g_images[e].assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        XR_CHECK(pxrEnumerateSwapchainImages(g_swapchain[e], n, &n,
                                             reinterpret_cast<XrSwapchainImageBaseHeader*>(g_images[e].data())),
                 "swapchain images");
    }
    LOG_I("xr", "per-eye swapchains %ux%u, %u images each", g_width, g_height, n);
    return true;
}

bool init(IDXGISwapChain* chain) {
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) return false;
    g_device->GetImmediateContext(&g_ctx);
    if (!(load_loader() && create_instance() && create_session() && create_swapchain(chain))) return false;
    if (!xr_input::create(g_instance, g_session, g_local, g_gipa)) LOG_W("xr", "controllers unavailable - keyboard/mouse/pad still work");
    return true;
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

bool copy_to_swapchain(int target, ID3D11Texture2D* src) {
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wsi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wsi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(pxrAcquireSwapchainImage(g_swapchain[target], &ai, &idx))) return false;
    bool ok = XR_SUCCEEDED(pxrWaitSwapchainImage(g_swapchain[target], &wsi));
    if (ok) g_ctx->CopySubresourceRegion(g_images[target][idx].texture, 0, 0, 0, 0, src, 0, nullptr);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    pxrReleaseSwapchainImage(g_swapchain[target], &ri);
    return ok;
}

// A texture like the back buffer, to hold a left-eye image.
bool ensure_hold(ID3D11Texture2D* back) {
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    if (g_hold) {
        D3D11_TEXTURE2D_DESC h;
        g_hold->GetDesc(&h);
        if (h.Width == d.Width && h.Height == d.Height && h.Format == d.Format) return true;
        g_hold->Release();
        g_hold = nullptr;
    }
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = 0;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    d.SampleDesc = {1, 0};
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, &g_hold))) {
        static bool logged = false;
        if (!logged) { logged = true; LOG_E("xr", "could not create the left-eye hold texture"); }
        return false;
    }
    return true;
}

void frame(IDXGISwapChain* chain) {
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(pxrWaitFrame(g_session, &wi, &fs))) return;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(pxrBeginFrame(g_session, &bi))) return;
    xr_input::sync(fs.predictedDisplayTime, g_state == XR_SESSION_STATE_FOCUSED);

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

    // Real eye height above the floor, for height calibration (every 30 s).
    static ULONGLONG last_height_log = 0;
    if (g_stage && GetTickCount64() - last_height_log > 30000) {
        XrSpaceLocation sl{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(pxrLocateSpace(g_view, g_stage, fs.predictedDisplayTime, &sl)) &&
            (sl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            last_height_log = GetTickCount64();
            LOG_I("xr", "your eye height above the floor: %.2f m", sl.pose.position.y);
        }
    }

    // Each eye's position, rotation and field of view relative to the head.
    {
        XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = g_view;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        XrView xv[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t count = 0;
        if (XR_SUCCEEDED(pxrLocateViews(g_session, &vli, &vs, 2, &count, xv)) && count == 2) {
            head_pose::Eye eyes[2];
            for (int i = 0; i < 2; ++i) {
                eyes[i].offset = {xv[i].pose.position.x, xv[i].pose.position.y, xv[i].pose.position.z};
                eyes[i].rotation = to_quat(xv[i].pose.orientation);
                eyes[i].fov[0] = xv[i].fov.angleLeft;
                eyes[i].fov[1] = xv[i].fov.angleRight;
                eyes[i].fov[2] = xv[i].fov.angleUp;
                eyes[i].fov[3] = xv[i].fov.angleDown;
            }
            head_pose::set_eyes(eyes[0], eyes[1]);
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_I("xr", "eyes: IPD %.1f mm; left FOV L%.1f R%.1f U%.1f D%.1f deg", (eyes[1].offset - eyes[0].offset).length() * 1000,
                      eyes[0].fov[0] * 57.3f, eyes[0].fov[1] * 57.3f, eyes[0].fov[2] * 57.3f, eyes[0].fov[3] * 57.3f);
            }
        }
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView views[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                 {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    uint32_t layer_count = 0;

    if (fs.shouldRender) {
        // Which eye this frame was rendered for (-1 = mono) and with which pose.
        int eye = head_pose::rendered_eye();
        head_pose::Pose used = head_pose::rendered_pose();
        bool mono = eye < 0;

        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
            if (mono) {
                if (copy_to_swapchain(0, back)) {
                    g_has_image[0] = true;
                    g_eye_pose[0] = used;
                }
                g_pending_left = false;
            } else if (eye == 0) {
                // Left eye: held back until its right eye exists, so a pair is always shown together
                // (PLAYTEST-2a: showing each eye as soon as it was drawn looked like vibration).
                if (ensure_hold(back)) {
                    g_ctx->CopyResource(g_hold, back);
                    g_hold_pose = used;
                    g_pending_left = true;
                }
            } else if (g_pending_left) {
                if (copy_to_swapchain(0, g_hold) && copy_to_swapchain(1, back)) {
                    g_has_image[0] = g_has_image[1] = true;
                    g_eye_pose[0] = g_hold_pose;
                    g_eye_pose[1] = used;
                    ++g_pairs;
                }
                g_pending_left = false;
            } else {
                ++g_orphans;  // right eye without a left one before it
            }
            back->Release();
        }

        static ULONGLONG last_pair_log = 0;
        if (GetTickCount64() - last_pair_log > 10000) {
            last_pair_log = GetTickCount64();
            LOG_I("xr", "stereo pairs shown: %llu (right eye without a left: %llu); game renders %.1f x %.1f deg",
                  (unsigned long long)g_pairs, (unsigned long long)g_orphans, 2 * std::atan(1.f / g_sx.load()) * 57.2958f,
                  2 * std::atan(1.f / g_sy.load()) * 57.2958f);
        }

        // Mono: both eyes show swapchain 0. Stereo: the latest complete pair, each eye at its own pose.
        bool ready = mono ? g_has_image[0] : (g_has_image[0] && g_has_image[1]);
        if (ready) {
            float half_x = std::atan(1.f / g_sx.load()), half_y = std::atan(1.f / g_sy.load());
            for (int e = 0; e < 2; ++e) {
                int src = mono ? 0 : e;
                const head_pose::Pose& p = g_eye_pose[src];
                views[e].pose.orientation = to_xr(p.rotation);
                views[e].pose.position = {p.position.x, p.position.y, p.position.z};
                views[e].fov = {-half_x, half_x, half_y, -half_y};
                views[e].subImage.swapchain = g_swapchain[src];
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

// The game's resolution changed (options menu, or a higher DSR resolution for sharpness):
// remake the per-eye swapchains at the new size.
void resize_if_needed(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(chain->GetDesc(&d)) || (d.BufferDesc.Width == g_width && d.BufferDesc.Height == g_height)) return;
    if (d.BufferDesc.Width < 64 || d.BufferDesc.Height < 64) return;  // minimised
    LOG_I("xr", "game resolution changed %ux%u -> %ux%u", g_width, g_height, d.BufferDesc.Width, d.BufferDesc.Height);
    for (int e = 0; e < 2; ++e) {
        if (g_swapchain[e]) pxrDestroySwapchain(g_swapchain[e]);
        g_swapchain[e] = XR_NULL_HANDLE;
        g_images[e].clear();
        g_has_image[e] = false;
    }
    g_pending_left = false;
    if (!create_swapchain(chain)) {
        g_failed = true;
        LOG_E("xr", "could not remake the swapchains - staying flat");
    }
}

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
    if (g_session_running) {
        resize_if_needed(chain);
        if (!g_failed) frame(chain);
    }
}

}  // namespace mecvr::xr_session

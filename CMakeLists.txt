cmake_minimum_required(VERSION 3.24)
project(mecvr LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so users don't need a redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(MECVR_VERSION "0.0.1")

# --- third party -------------------------------------------------------------
add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# --- core library (everything testable without the game) ---------------------
add_library(mecvr_core STATIC
    src/diagnostics/log.cpp
    src/core/config.cpp
    src/core/sigscan.cpp)
target_include_directories(mecvr_core PUBLIC src third_party third_party/openxr/include)
target_compile_definitions(mecvr_core PUBLIC MECVR_VERSION="${MECVR_VERSION}" WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(mecvr_core PUBLIC /W4 /utf-8)

# --- the proxy DLL the game loads --------------------------------------------
add_library(mecvr SHARED
    src/core/dllmain.cpp
    src/core/proxy.cpp
    src/core/proxy.def
    src/core/proxy_stubs.asm
    src/core/game_version.cpp
    src/core/window_title.cpp
    src/render/present_hook.cpp
    src/diagnostics/typeinfo_dump.cpp
    src/diagnostics/recon_probe.cpp
    src/render/cb_capture.cpp
    src/render/cam_hunt.cpp
    src/diagnostics/hwbp.cpp
    src/diagnostics/code_dump.cpp
    src/core/head_pose.cpp
    src/render/camera_override.cpp
    src/render/xr_session.cpp
    src/diagnostics/bug_report.cpp
    src/diagnostics/view_recon.cpp
    src/render/stereo_views.cpp
    src/render/screenshot.cpp
    src/diagnostics/time_recon.cpp
    src/render/time_sync.cpp
    src/render/ctx_hooks.cpp
    src/render/taa_eyes.cpp
    src/diagnostics/mem_find.cpp
    src/render/motion_fix.cpp
    src/input/xinput_hook.cpp
    src/input/xr_input.cpp
    src/input/yaw_drive.cpp)
target_link_libraries(mecvr PRIVATE mecvr_core minhook dxguid)
set_target_properties(mecvr PROPERTIES OUTPUT_NAME "mecvr")

# --- unit checks ---------------------------------------------------------------
enable_testing()
add_executable(mecvr_tests tests/test_main.cpp tests/test_config.cpp tests/test_sigscan.cpp tests/test_vmath.cpp tests/test_pad_map.cpp tests/test_yaw_model.cpp)
target_link_libraries(mecvr_tests PRIVATE mecvr_core)
target_include_directories(mecvr_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME unit COMMAND mecvr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

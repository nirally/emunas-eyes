#pragma once
#include <filesystem>

struct IDXGISwapChain;

namespace mecvr::screenshot {

// Render thread, once per Present: SHOT / PAIR trigger files, and request().
void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir);
// PLAYTEST-8a ("if I could take a screenshot in game I would do that but I can't do it while im wearing the
// headset"): from the controllers (hold Y + B); the next presented frame is saved as vrshot_<date>_<time>.bmp.
void request();

}  // namespace mecvr::screenshot

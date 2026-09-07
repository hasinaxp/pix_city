#define _CRT_SECURE_NO_WARNINGS
#include "game/city_game.hpp"

// ---- which GPU this runs on ----
//
// A laptop has two, and the one it hands an OpenGL context to by default is
// the integrated one. That is not a small difference here: this renderer is
// bound by the driver's per-draw cost far more than by anything the GPU does,
// and Intel's OpenGL driver charges something like twenty microseconds a draw
// where NVIDIA's charges one or two. Six hundred draws a frame is the whole
// frame budget on one and a rounding error on the other.
//
// Both vendors watch for these two exported symbols in the executable and use
// them to pick the high-performance adapter. They have to be exported from the
// .exe itself, which is why they live here and not in the engine.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int main() {
    run_city_game();
    return 0;
}

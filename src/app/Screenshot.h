#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <functional>

namespace micronotes::app {

// Draws frames until the compositor has mapped and sized the window, then reads
// the window back and writes it to `path`. Returns a process exit code.
//
// `drawFrame` is handed the window's current size; it is the caller's whole
// frame, because this unit has no opinion about what a frame contains. That
// separation is the point: pumping events until a window is mapped, choosing
// PNG or BMP, and reporting a save failure have nothing to do with the note
// application, and they were sitting in the middle of it.
int captureWindowToFile(SDL_Renderer* renderer, const std::filesystem::path& path,
                        int fallbackWidth, int fallbackHeight,
                        const std::function<void(int, int)>& drawFrame);

}

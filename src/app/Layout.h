#pragma once

#include "ui/ShellLayout.h"

// Where the window's panels are, for the runtime as it stands.
//
// `ui::computeShellLayout` is the arithmetic and takes plain inputs so it can
// be tested; this is the one place that assembles those inputs from a live
// `UiRuntime`, which is what makes the rects a frame paints with, the rects it
// is hit-tested against and the rects the tests assert on the same rects.
//
// Six callers -- the frame, the cursor shape, the pointer router, the wheel,
// the window chrome and the loop -- and it lived in `app/Shell.h` for want of
// anywhere better, which put a layout pass in the header that describes state.
namespace micronotes::app {

struct UiRuntime;
using micronotes::ui::ShellLayout;

// Every caller goes through here so that the rects a frame is painted with, the
// rects it is hit-tested against and the rects the tests assert on are the same
// rects. `ui.layoutMode` is both an input and an output: feeding the last mode
// back in is what gives the compact breakpoint its hysteresis.
ShellLayout shellLayout(UiRuntime& ui, int width, int height);

}

#pragma once

#include "ui/ScrollList.h"

// How far one notch of the wheel moves each scrolling surface.
//
// The arithmetic that makes a trackpad work, and the offset-plus-ceiling every
// one of these surfaces keeps, are `ui::ScrollList`. What is left here is the
// per-surface policy, which is the part that is about this app: in lines for
// the raw editor, which scrolls by row; in pixels for everything else, which
// scrolls by distance. Together in one header rather than beside their own call
// sites, because the only way to tell whether two surfaces scroll at the same
// rate is to read the numbers next to each other.
namespace micronotes::app {

using ui::ScrollList;

constexpr int kEditorPageLines = 20;

constexpr float kEditorScrollLinesPerNotch = 3.0f;
constexpr float kViewerScrollPixelsPerNotch = 42.0f;
constexpr float kLiveScrollPixelsPerNotch = 42.0f;
constexpr float kSidebarScrollPixelsPerNotch = 42.0f;
constexpr float kRightPanelScrollPixelsPerNotch = 42.0f;

}

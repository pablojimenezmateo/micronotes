#include "ui/RibbonLayout.h"

#include "ui/Metrics.h"

#include <algorithm>
#include <cmath>

namespace micronotes::ui {
namespace {

// What each control does, and which end of the rail it sits at.
constexpr std::array<RibbonControl, kRibbonControlCount> kControls {{
  // Top group: the four ways to start doing something.
  {ActionId::NewNote, RibbonMark::NewNote, false},
  {ActionId::GoToNote, RibbonMark::GoToNote, false},
  {ActionId::SearchAllNotes, RibbonMark::Search, false},
  {ActionId::CommandPalette, RibbonMark::Commands, false},
  // Foot: the arrangement of the window, and the way into its settings.
  {ActionId::ToggleSidebar, RibbonMark::LeftPanel, true},
  {ActionId::ToggleRightPanel, RibbonMark::RightPanel, true},
  {ActionId::Settings, RibbonMark::Settings, true},
}};

// Which control the rail gives up first when the column cannot hold all seven,
// and which it holds on to longest.
//
// The rail exists to be the way back into a shell with every panel put away, so
// the sidebar toggle is the last thing to go; Settings, which both the palette
// and its own chord reach, is the first. The rail used to place the top group
// downwards and the foot group upwards and simply drop whatever did not fit,
// which took the two panel toggles first -- so a short window with the sidebar
// hidden had no drawn way to bring it back, which is the one state the rail is
// there to prevent.
constexpr std::array<ActionId, kRibbonControlCount> kGivenUpFirst {{
  ActionId::Settings,
  ActionId::CommandPalette,
  ActionId::ToggleRightPanel,
  ActionId::SearchAllNotes,
  ActionId::GoToNote,
  ActionId::NewNote,
  ActionId::ToggleSidebar,
}};

// How many controls a column this tall can hold: n of them occupy n button
// heights and n-1 gaps, inside a pad at each end.
std::size_t controlsThatFit(float height) {
  const float step = kRibbonButtonSize + kRibbonGap;
  const float room = height - 2.0f * kRibbonEdgePad + kRibbonGap;
  if(room < step) return 0;
  return std::min(static_cast<std::size_t>(room / step), kRibbonControlCount);
}

}

std::span<const RibbonControl> ribbonControls() {
  return {kControls.data(), kControls.size()};
}

RibbonPlacement ribbonLayout(Rect rect) {
  RibbonPlacement out;
  if(rect.w <= 0.0f || rect.h <= 0.0f) return out;

  const std::size_t keep = controlsThatFit(rect.h);
  std::array<bool, kRibbonControlCount> given {};
  for(std::size_t i = 0; i + keep < kRibbonControlCount; ++i) {
    for(std::size_t j = 0; j < kControls.size(); ++j) {
      if(kControls[j].action == kGivenUpFirst[i]) given[j] = true;
    }
  }

  const float step = kRibbonButtonSize + kRibbonGap;
  const float x = std::round(rect.x + (rect.w - kRibbonButtonSize) / 2.0f);

  // The foot group is bottom-anchored as a group, so the pair of panel toggles
  // keeps its shape however many of the three survived.
  std::size_t footKept = 0;
  for(std::size_t i = 0; i < kControls.size(); ++i) {
    if(kControls[i].atFoot && !given[i]) ++footKept;
  }

  float top = rect.y + kRibbonEdgePad;
  float foot = rect.y + rect.h - kRibbonEdgePad - kRibbonButtonSize
             - step * static_cast<float>(footKept > 0 ? footKept - 1 : 0);
  for(std::size_t i = 0; i < kControls.size(); ++i) {
    if(given[i]) continue;
    float& cursor = kControls[i].atFoot ? foot : top;
    out.controls[out.count++] = {kControls[i], Rect {x, std::round(cursor), kRibbonButtonSize, kRibbonButtonSize}};
    cursor += step;
  }
  return out;
}

}

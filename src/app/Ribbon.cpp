#include "app/Ribbon.h"

#include "app/Shell.h"

#include "ui/Metrics.h"
#include "ui/Theme.h"

#include <array>
#include <cmath>
#include <string>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::fill;
using ui::fillRounded;
using ui::stroke;
using ui::theme;

// What each control does, and which way up the rail it sits.
//
// Drawn rather than typeset, every one of them: the vendored UI face has no
// glyph for a page, a magnifier or a pane, and the emoji face is not installed
// everywhere. A mark assembled from lines and rects is the only one that is
// certain to be there and certain to stay crisp at any display scale.
enum class Mark {
  NewNote,
  GoToNote,
  Search,
  Commands,
  LeftPanel,
  RightPanel,
  Settings
};

struct Control {
  ui::ActionId action = ui::ActionId::Count;
  Mark mark = Mark::NewNote;
};

// Top group: the four ways to start doing something.
constexpr std::array<Control, 4> kLeading {{
  {ui::ActionId::NewNote, Mark::NewNote},
  {ui::ActionId::GoToNote, Mark::GoToNote},
  {ui::ActionId::SearchAllNotes, Mark::Search},
  {ui::ActionId::CommandPalette, Mark::Commands},
}};

// Foot: the arrangement of the window, and the way into its settings. At the
// foot because they are what you reach for having finished, not started.
constexpr std::array<Control, 3> kTrailing {{
  {ui::ActionId::ToggleSidebar, Mark::LeftPanel},
  {ui::ActionId::ToggleRightPanel, Mark::RightPanel},
  {ui::ActionId::Settings, Mark::Settings},
}};

constexpr float kGap = 4.0f;
constexpr float kEdgePad = 6.0f;

void line(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, x1, y1, x2, y2);
}

// A pane divided in two, with the half this control governs filled in. The two
// panel toggles are the same mark mirrored, which is what makes them read as a
// pair rather than as two unrelated rectangles.
void drawPanelMark(SDL_Renderer* renderer, Rect box, bool leading, bool on, SDL_Color color) {
  stroke(renderer, box, color);
  const float split = std::round(box.x + (leading ? box.w * 0.4f : box.w * 0.6f));
  line(renderer, split, box.y, split, box.y + box.h - 1.0f, color);
  if(!on) return;
  const Rect filled = leading ? Rect {box.x + 1.0f, box.y + 1.0f, split - box.x - 1.0f, box.h - 2.0f}
                              : Rect {split + 1.0f, box.y + 1.0f, box.x + box.w - split - 2.0f, box.h - 2.0f};
  fill(renderer, filled, color);
}

void drawMark(SDL_Renderer* renderer, Mark mark, Rect box, bool on, SDL_Color color) {
  const float cy = std::round(box.y + box.h / 2.0f);
  switch(mark) {
    case Mark::NewNote: {
      // A page, with a plus where the corner fold would be.
      const Rect page {box.x + 1.0f, box.y, box.w - 5.0f, box.h};
      stroke(renderer, page, color);
      ui::hLine(renderer, page.x + 3.0f, page.x + page.w - 4.0f, page.y + 5.0f, color);
      ui::hLine(renderer, page.x + 3.0f, page.x + page.w - 4.0f, page.y + 9.0f, color);
      const float px = box.x + box.w - 2.0f;
      const float py = box.y + box.h - 3.0f;
      ui::hLine(renderer, px - 3.0f, px + 3.0f, py, color);
      line(renderer, px, py - 3.0f, px, py + 3.0f, color);
      return;
    }
    case Mark::GoToNote: {
      // A page with an arrow into it: going to a note, rather than looking for
      // one, which is what the magnifier beneath it means.
      // The arrow sits clear of the page rather than crossing its border: two
      // strokes meeting at a right angle turn into a smudge at sixteen pixels.
      stroke(renderer, {box.x, box.y + 2.0f, box.w - 7.0f, box.h - 4.0f}, color);
      const float ax = box.x + box.w - 1.0f;
      ui::hLine(renderer, ax - 5.0f, ax, cy, color);
      line(renderer, ax - 3.0f, cy - 3.0f, ax, cy, color);
      line(renderer, ax - 3.0f, cy + 3.0f, ax, cy, color);
      return;
    }
    case Mark::Search: {
      // A ring and a handle. Drawn as a rounded outline rather than a circle:
      // at sixteen pixels the difference is invisible and the outline is exact.
      ui::strokeRounded(renderer, {box.x, box.y, box.w - 5.0f, box.h - 5.0f}, color, (box.w - 5.0f) / 2.0f);
      line(renderer, box.x + box.w - 6.0f, box.y + box.h - 6.0f, box.x + box.w - 1.0f, box.y + box.h - 1.0f, color);
      return;
    }
    case Mark::Commands: {
      // A prompt: a chevron and the line it types on.
      line(renderer, box.x + 2.0f, cy - 4.0f, box.x + 6.0f, cy, color);
      line(renderer, box.x + 6.0f, cy, box.x + 2.0f, cy + 4.0f, color);
      ui::hLine(renderer, box.x + 8.0f, box.x + box.w - 1.0f, cy + 4.0f, color);
      return;
    }
    case Mark::LeftPanel:
      drawPanelMark(renderer, {box.x, box.y + 1.0f, box.w, box.h - 2.0f}, true, on, color);
      return;
    case Mark::RightPanel:
      drawPanelMark(renderer, {box.x, box.y + 1.0f, box.w, box.h - 2.0f}, false, on, color);
      return;
    case Mark::Settings: {
      // Three sliders. A cogwheel is the usual mark and the wrong one to draw
      // from line segments: the teeth alias into a smudge at this size.
      const float rows[] = {cy - 5.0f, cy, cy + 5.0f};
      const float knobs[] = {box.x + 4.0f, box.x + box.w - 5.0f, box.x + 8.0f};
      for(int i = 0; i < 3; ++i) {
        ui::hLine(renderer, box.x, box.x + box.w - 1.0f, rows[i], color);
        fill(renderer, {knobs[i] - 1.0f, rows[i] - 2.0f, 3.0f, 5.0f}, color);
      }
      return;
    }
  }
}

// Whether a toggle is currently on, so its mark can say so. Everything else in
// the rail is a verb and has no state to show.
bool controlIsOn(const UiRuntime& ui, ui::ActionId action) {
  const auto& workspace = ui.state.workspace();
  if(action == ui::ActionId::ToggleSidebar) return workspace.sidebarVisible;
  if(action == ui::ActionId::ToggleRightPanel) return workspace.rightPanelVisible;
  return false;
}

// Where every control is drawn, top group down from the top and foot group up
// from the bottom. One function, called by the draw and by both hit tests, so
// what was painted and what a click lands on cannot disagree.
//
// A window too short to hold all seven drops controls from the foot group
// upward -- Settings first. Overlapping two marks would leave a smear that
// answers to whichever hit test ran last, and a control that is not there is at
// least honest about it.
template <typename Visit>
void eachControl(Rect rect, const Visit& visit) {
  const float step = ui::kRibbonButtonSize + kGap;
  const float x = std::round(rect.x + (rect.w - ui::kRibbonButtonSize) / 2.0f);

  float y = rect.y + kEdgePad;
  float leadingBottom = y;
  for(const auto& control : kLeading) {
    if(y + ui::kRibbonButtonSize > rect.y + rect.h) break;
    visit(control, Rect {x, std::round(y), ui::kRibbonButtonSize, ui::kRibbonButtonSize});
    y += step;
    leadingBottom = y;
  }

  float bottom = rect.y + rect.h - kEdgePad - ui::kRibbonButtonSize;
  for(auto it = kTrailing.rbegin(); it != kTrailing.rend(); ++it) {
    if(bottom < leadingBottom) break;
    visit(*it, Rect {x, std::round(bottom), ui::kRibbonButtonSize, ui::kRibbonButtonSize});
    bottom -= step;
  }
}

// The tooltip names the action and the keys that also run it, so the rail
// teaches the shortcut rather than replacing it.
std::string controlTooltip(const ui::ActionSpec& spec) {
  std::string label(spec.label);
  // "Settings..." promises a dialog on the row of a palette. On a tooltip over
  // a single button the ellipsis says nothing the button did not already.
  if(label.size() > 3 && label.compare(label.size() - 3, 3, "...") == 0) label.resize(label.size() - 3);
  const std::string keys = ui::acceleratorText(spec);
  return keys.empty() ? label : label + "  " + keys;
}

}

void drawRibbon(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rect) {
  (void)text;
  if(rect.w <= 0.0f || rect.h <= 0.0f) return;
  fill(renderer, rect, theme().sidebarBg);
  ui::ClipGuard clip(renderer, rect);

  eachControl(rect, [&](const Control& control, Rect box) {
    const ui::ActionSpec* spec = ui::findAction(control.action);
    if(!spec) return;
    const bool on = controlIsOn(ui, control.action);
    const bool hot = ui.hovered(box);
    if(hot) fillRounded(renderer, box, theme().hoverBg, ui::kRadiusSmall);
    // A toggle that is on stays lit with the pointer away from it; everything
    // else in the rail goes back to the muted ink once the pointer leaves.
    const SDL_Color ink = hot ? theme().text : (on ? theme().accent : theme().muted);
    const Rect mark {std::round(box.x + (box.w - ui::kRibbonIconSize) / 2.0f),
                     std::round(box.y + (box.h - ui::kRibbonIconSize) / 2.0f),
                     ui::kRibbonIconSize, ui::kRibbonIconSize};
    drawMark(renderer, control.mark, mark, on, ink);
    ui.offerTooltip(box, controlTooltip(*spec));
  });
}

std::optional<ui::ActionId> handleRibbonClick(UiRuntime& ui, Rect rect, float x, float y) {
  (void)ui;
  if(!ui::contains(rect, x, y)) return std::nullopt;
  std::optional<ui::ActionId> hit;
  eachControl(rect, [&](const Control& control, Rect box) {
    if(ui::contains(box, x, y)) hit = control.action;
  });
  return hit;
}

bool ribbonHasControlAt(UiRuntime& ui, Rect rect, float x, float y) {
  (void)ui;
  if(!ui::contains(rect, x, y)) return false;
  bool over = false;
  eachControl(rect, [&](const Control&, Rect box) {
    if(ui::contains(box, x, y)) over = true;
  });
  return over;
}

}

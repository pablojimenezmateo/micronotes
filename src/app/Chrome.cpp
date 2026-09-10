#include "app/Chrome.h"

#include "ui/Glyphs.h"
#include "ui/Painter.h"

#include <cmath>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::hLine;
using ui::stroke;

}

const char* paneModeName(ui::PaneMode mode) {
  switch(mode) {
    case ui::PaneMode::Editor: return "Raw Markdown";
    case ui::PaneMode::Viewer: return "Reading";
    case ui::PaneMode::Split: break;
  }
  return "Split";
}


void drawNoteIcon(SDL_Renderer* renderer, std::string_view icon, Rect box, SDL_Color color) {
  if(ui::drawNoteGlyph(renderer, icon, box, color)) return;
  // No icon, or one this version does not know: the page mark every note wears
  // by default. Kept here rather than in the glyph set because it is not a
  // choice on the picker -- it is what "no icon" looks like.
  const float w = 9.0f;
  const float h = 11.0f;
  const float left = std::round(box.x + (box.w - w) / 2.0f);
  const float topY = std::round(box.y + (box.h - h) / 2.0f);
  stroke(renderer, {left, topY, w, h}, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 4.0f, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 7.0f, color);
}

}

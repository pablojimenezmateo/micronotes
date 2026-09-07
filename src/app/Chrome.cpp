#include "app/Chrome.h"

#include <cstdint>

#include "app/Shell.h"

#include "core/perf/Perf.h"

#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::ellipsizeToWidth;
using ui::fill;
using ui::hLine;
using ui::stroke;
using ui::theme;

// Words and characters in the buffer.
//
// A word is a run of non-space bytes, which is what every editor's status bar
// means by the word and what a reader checking a word budget expects. The
// editor carries both numbers, so there is nothing to memoise and nothing to
// walk.
//
// This was a memo keyed on the buffer's revision, which stopped it running on
// every *frame* -- a scroll, a hover and a window focus all draw one and none of
// them touches the text. What it could not stop was running on every
// *keystroke*, which is exactly when the revision does move, and that is a
// byte-at-a-time walk of the whole note: 247 us on a 200 KB one, against the
// ~14 us that keystroke's own layout update costs, and the largest single thing
// a keystroke did.
//
// `MarkdownEditor::splice` carries the count across each edit instead, so the
// question is now a member read and the memo, its struct on `UiRuntime` and its
// two counters are all gone.

std::string plural(std::size_t count, std::string_view noun) {
  return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
}

}

void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  // What the note is, on the right, and what just happened, on the left.
  //
  // The left of this bar used to be a key legend -- go to note, commands,
  // shortcuts -- which is a menu bar written in a font too small to be one. F1
  // holds every shortcut, can be searched, and is itself in the legend, so the
  // legend was three keys advertising the fourth. The counts that replace it
  // are the one thing about the open note that nothing else on screen says.
  //
  // The bar's own baseline, once. Every string on it is one line of UI text in
  // a strip of fixed height, so where that line sits is arithmetic over the two
  // -- and it was written out as `rect.y + 6` four times, which is the number
  // the medium text size happens to want.
  const ui::TextStyle style = ui::chromeStyle();
  const float baseline = ui::textTop(rect, text, style);
  const float dot = 6.0f;
  fill(renderer, {rect.x + ui::kSpace3, std::round(rect.y + (rect.h - dot) / 2.0f), dot, dot},
       ui.editor.dirty() ? theme().warn : theme().accent);

  std::string left = ui.status;
  if(ui.focus == FocusArea::Search) left = "Search all: " + ui.search.text() + "    Enter open  Esc clear";
  else if(ui.focus == FocusArea::Find) left = "Find in note: " + ui.find.text() + "    Esc close";
  else if(ui.editor.dirty()) left = "Unsaved changes";

  // The right first, so the left knows how much room it was left with.
  float right = rect.x + rect.w - ui::kSpace3;
  if(!ui.state.selection().noteId.empty()) {
    // Characters are bytes of the note as stored, not codepoints; saying so
    // here is cheaper than a UTF-8 walk nobody asked for.
    const std::string tally = plural(ui.editor.wordCount(), "word") + "    " +
                              plural(ui.editor.text().size(), "character");
    const float width = static_cast<float>(text.width(tally, style));
    text.draw(tally, right - width, baseline, theme().chromeTextSecondary, style);
    right -= width + ui::kSpace4;
  }
  const std::string mode = paneModeName(ui.state.workspace().paneMode());
  const float modeWidth = static_cast<float>(text.width(mode, style));
  text.draw(mode, right - modeWidth, baseline, theme().chromeTextSecondary, style);
  right -= modeWidth;

  if(left.empty()) return;
  // After the unsaved dot, with the same gap the dot has from the edge.
  const float leftX = rect.x + ui::kSpace3 + dot + ui::kSpace2;
  const float room = right - leftX - ui::kSpace4;
  text.draw(ellipsizeToWidth(text, left, static_cast<int>(room), style), leftX, baseline,
            theme().chromeText, style);
}

const char* paneModeName(ui::PaneMode mode) {
  switch(mode) {
    case ui::PaneMode::Editor: return "Raw Markdown";
    case ui::PaneMode::Viewer: return "Reading";
    case ui::PaneMode::Split: return "Split";
    case ui::PaneMode::Live: break;
  }
  return "Live";
}


void drawNoteIcon(SDL_Renderer* renderer, TextRenderer& text, std::string_view icon, Rect box, SDL_Color color) {
  if(text.drawIcon(icon, box, color)) return;
  // No emoji face anywhere, or no icon set: a drawn page mark rather than the
  // tofu box the missing glyph would otherwise leave behind.
  const float w = 9.0f;
  const float h = 11.0f;
  const float left = std::round(box.x + (box.w - w) / 2.0f);
  const float topY = std::round(box.y + (box.h - h) / 2.0f);
  stroke(renderer, {left, topY, w, h}, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 4.0f, color);
  hLine(renderer, left + 2.0f, left + w - 2.0f, topY + 7.0f, color);
}

}

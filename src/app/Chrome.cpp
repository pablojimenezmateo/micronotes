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

// The favourite star's target, and the icon in front of the note's own crumb.
// Both were written out inline, and the star's was written out twice -- once
// as its own left edge and once as the reserve the breadcrumb stops at.
constexpr float kFavoriteWidth = 26.0f;
constexpr float kCrumbIconSize = 16.0f;

// A window button's mark, drawn from lines rather than typeset: the UI face has
// no glyph for a close cross, and a missing one would leave tofu where the
// window controls should be.
void drawWindowGlyph(SDL_Renderer* renderer, Rect box, std::size_t which, bool maximized, SDL_Color color) {
  const float cx = std::round(box.x + box.w / 2.0f);
  const float cy = std::round(box.y + box.h / 2.0f);
  // The helpers set the draw colour themselves; the raw lines below are the
  // only ones that have to say so, so they say so immediately before drawing.
  const auto line = [&](float x1, float y1, float x2, float y2) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, x1, y1, x2, y2);
  };
  if(which == 0) {
    hLine(renderer, cx - 5.0f, cx + 5.0f, cy, color);
    return;
  }
  if(which == 1) {
    if(maximized) {
      // Two offset outlines: the restored window in front of the space it
      // currently fills.
      stroke(renderer, {cx - 5.0f, cy - 2.0f, 8.0f, 8.0f}, color);
      hLine(renderer, cx - 2.0f, cx + 3.0f, cy - 5.0f, color);
      line(cx + 3.0f, cy - 5.0f, cx + 3.0f, cy + 1.0f);
    } else {
      stroke(renderer, {cx - 5.0f, cy - 5.0f, 10.0f, 10.0f}, color);
    }
    return;
  }
  line(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f);
  line(cx - 5.0f, cy + 5.0f, cx + 5.0f, cy - 5.0f);
}

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
  const ui::TextStyle style {};
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
    const float width = static_cast<float>(text.width(tally));
    text.draw(tally, right - width, baseline, theme().textMuted);
    right -= width + ui::kSpace4;
  }
  const std::string mode = paneModeName(ui.state.workspace().paneMode());
  const float modeWidth = static_cast<float>(text.width(mode));
  text.draw(mode, right - modeWidth, baseline, theme().textMuted);
  right -= modeWidth;

  if(left.empty()) return;
  // After the unsaved dot, with the same gap the dot has from the edge.
  const float leftX = rect.x + ui::kSpace3 + dot + ui::kSpace2;
  const float room = right - leftX - ui::kSpace4;
  text.draw(ellipsizeToWidth(text, left, static_cast<int>(room), false, false), leftX, baseline,
            theme().textSecondary);
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

void drawTitleBar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui.crumbs.clear();
  ui.favoriteButton = {};
  ui.windowButtons = {};
  fill(renderer, rect, theme().chromeBackground);
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().border);
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().small};
  const auto note = ui.state.hasLibrary() ? ui.state.findNote(ui.state.selection().noteId) : std::nullopt;

  // The same rule the status bar and every row in the shell uses. This was
  // `rect.y + max(4, (rect.h - line) / 2)`, unrounded and with a floor of its
  // own, so the one strip of chrome at the top of the window centred its text
  // by a slightly different arithmetic from the one at the bottom -- which is
  // exactly what `ui::textTop` exists to stop.
  const float baseline = ui::textTop(rect, text, style);

  // The window controls first, right to left so close sits in the actual
  // corner, where the pointer lands when it is thrown at it. Everything else
  // in the strip then lays out against whatever they left.
  float right = rect.x + rect.w;
  if(ui.customChrome) {
    for(std::size_t i = 0; i < ui.windowButtons.size(); ++i) {
      Rect box {rect.x + rect.w - ui::kWindowButtonWidth * static_cast<float>(3 - i), rect.y,
                ui::kWindowButtonWidth, rect.h - 1.0f};
      ui.windowButtons[i] = box;
      const bool hot = ui.hovered(box);
      // Close goes red on hover; the other two take the ordinary hover fill.
      if(hot) fill(renderer, box, i == 2 ? theme().warn : theme().rowHighlight);
      const SDL_Color mark = hot ? (i == 2 ? theme().onAccent : theme().textPrimary) : theme().textMuted;
      drawWindowGlyph(renderer, box, i, ui.windowMaximized, mark);
    }
    ui.offerTooltip(ui.windowButtons[0], "Minimize");
    ui.offerTooltip(ui.windowButtons[1], ui.windowMaximized ? "Restore" : "Maximize");
    ui.offerTooltip(ui.windowButtons[2], "Close");
    right = ui.windowButtons.front().x;
  }

  float x = rect.x + ui::kSpace4 + ui::kSpace1;
  // Whatever the star needs, so the trail cannot run under it. It was a bare
  // 44 against a star placed at `right - 34` with a width of 26, which is two
  // numbers that have to be kept in step by hand.
  const float limit = right - kFavoriteWidth - ui::kSpace2;

  // Every crumb down to the note's own folder, root first.
  std::vector<std::filesystem::path> trail {{}};
  if(note) {
    std::filesystem::path walk;
    for(const auto& part : note->folder) {
      walk /= part;
      trail.push_back(walk);
    }
  }
  if(!ui.state.hasLibrary()) trail.clear();
  for(std::size_t i = 0; i < trail.size() && x < limit; ++i) {
    const auto label = trail[i].empty() ? ui.state.libraryRoot().filename().generic_string()
                                        : trail[i].filename().generic_string();
    const float w = static_cast<float>(text.width(label, style));
    const Rect hit {x - ui::kSpace1, rect.y + ui::kSpace1, w + ui::kSpace2, rect.h - ui::kSpace2};
    const bool hot = ui.hovered(hit);
    if(hot) fill(renderer, hit, theme().rowHighlight);
    text.draw(label, x, baseline, hot ? theme().textPrimary : theme().textSecondary, style);
    ui.crumbs.emplace_back(hit, trail[i]);
    x += w + ui::kSpace2;
    text.draw("/", x, baseline, theme().textMuted, style);
    x += static_cast<float>(text.width("/", style)) + ui::kSpace2;
  }
  if(note && x < limit) {
    drawNoteIcon(renderer, text, note->icon,
                 {x, std::round(rect.y + (rect.h - kCrumbIconSize) / 2.0f), kCrumbIconSize, kCrumbIconSize},
                 theme().textMuted);
    x += kCrumbIconSize + ui::kSpace1;
    text.draw(ellipsizeToWidth(text, note->title, static_cast<int>(limit - x), style), x, baseline, theme().textPrimary, style);
  }

  if(note) {
    // A filled star reads as "kept"; the outline is an offer.
    ui.favoriteButton = {right - kFavoriteWidth - ui::kSpace2, rect.y + ui::kSpace1, kFavoriteWidth,
                         rect.h - ui::kSpace2};
    const bool pinned = ui.state.favorite(note->id);
    ui.offerTooltip(ui.favoriteButton, pinned ? "Remove from favorites" : "Add to favorites");
    if(ui.hovered(ui.favoriteButton)) fill(renderer, ui.favoriteButton, theme().rowHighlight);
    const auto star = pinned ? "\xe2\x98\x85" : "\xe2\x98\x86";
    text.draw(star,
              std::round(ui.favoriteButton.x +
                         (ui.favoriteButton.w - static_cast<float>(text.width(star, style))) / 2.0f),
              baseline, pinned ? theme().accent : theme().textMuted, style);
  }
}

}

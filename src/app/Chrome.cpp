#include "app/Chrome.h"

#include <cstdint>

#include "app/Shell.h"

#include "core/perf/Perf.h"

#include "ui/Actions.h"
#include "ui/Metrics.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"

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

// Words and characters in the buffer, in one pass.
//
// A word is a run of non-space bytes, which is what every editor's status bar
// means by the word and what a reader checking a word budget expects.
// Characters are bytes of the note as stored, not codepoints; saying so here
// is cheaper than a UTF-8 walk nobody asked for.
struct BufferCounts {
  std::size_t words = 0;
  std::size_t characters = 0;
};

// Memoised on the buffer's revision, because this used to run on every frame.
//
// The comment that stood here said it ran "about once per keystroke", on the
// reasoning that frames are event driven. That was wrong, and the counters said
// so the moment they existed: status.word_counts tracked frame.presents exactly,
// because a scroll, a hover and a window focus all draw a frame and none of them
// touches the text. It was a byte-at-a-time walk of the whole note, 0.1 ms a
// frame on a 235 KB one, to render a number that had not changed.
//
// The revision is the editor's, so a buffer that has not been edited cannot be
// recounted no matter what else happened. `kNoRevision` is a value the editor
// never issues, so the first call always counts.
constexpr std::uint64_t kNoRevision = static_cast<std::uint64_t>(-1);

BufferCounts countBufferUncached(std::string_view text) {
  perf::ScopeTimer timer("status.count_buffer");
  perf::addCounter(perf::CounterId::StatusWordCounts);
  BufferCounts counts;
  counts.characters = text.size();
  bool inWord = false;
  for(const unsigned char c : text) {
    const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if(space) {
      inWord = false;
      continue;
    }
    if(!inWord) ++counts.words;
    inWord = true;
  }
  return counts;
}

BufferCounts countBuffer(std::string_view text, std::uint64_t revision) {
  static std::uint64_t cachedRevision = kNoRevision;
  static BufferCounts cached;
  if(revision == cachedRevision) {
    perf::addCounter(perf::CounterId::StatusWordCountsReused);
    return cached;
  }
  cached = countBufferUncached(text);
  cachedRevision = revision;
  return cached;
}

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
  fill(renderer, {rect.x + 12, rect.y + 7, 6, 6}, ui.editor.dirty() ? theme().warn : theme().accentDim);

  std::string left = ui.status;
  if(ui.focus == FocusArea::Search) left = "Search all: " + ui.search.text() + "    Enter open  Esc clear";
  else if(ui.focus == FocusArea::Find) left = "Find in note: " + ui.find.text() + "    Esc close";
  else if(ui.editor.dirty()) left = "Unsaved changes";

  // The right first, so the left knows how much room it was left with.
  float right = rect.x + rect.w - 14.0f;
  if(!ui.state.selection().noteId.empty()) {
    const BufferCounts counts = countBuffer(ui.editor.text(), ui.editor.revision());
    const std::string tally = plural(counts.words, "word") + "    " + plural(counts.characters, "character");
    const float width = static_cast<float>(text.width(tally));
    text.draw(tally, right - width, rect.y + 6, theme().dim);
    right -= width + 16.0f;
  }
  const std::string mode = paneModeName(ui.state.workspace().paneMode());
  const float modeWidth = static_cast<float>(text.width(mode));
  text.draw(mode, right - modeWidth, rect.y + 6, theme().dim);
  right -= modeWidth;

  if(left.empty()) return;
  const float room = right - (rect.x + 28.0f) - 16.0f;
  text.draw(ellipsizeToWidth(text, left, static_cast<int>(room), false, false), rect.x + 28, rect.y + 6,
            theme().muted);
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
  fill(renderer, rect, theme().statusBg);
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().hairline);
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().small};
  const auto note = ui.state.hasLibrary() ? ui.state.findNote(ui.state.selection().noteId) : std::nullopt;

  const float baseline = rect.y + std::max(4.0f, (rect.h - static_cast<float>(text.lineHeight(style))) / 2.0f);

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
      if(hot) fill(renderer, box, i == 2 ? theme().warn : theme().hoverBg);
      const SDL_Color mark = hot ? (i == 2 ? theme().onAccent : theme().text) : theme().dim;
      drawWindowGlyph(renderer, box, i, ui.windowMaximized, mark);
    }
    ui.offerTooltip(ui.windowButtons[0], "Minimize");
    ui.offerTooltip(ui.windowButtons[1], ui.windowMaximized ? "Restore" : "Maximize");
    ui.offerTooltip(ui.windowButtons[2], "Close");
    right = ui.windowButtons.front().x;
  }

  float x = rect.x + 20.0f;
  const float limit = right - 44.0f;

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
    const Rect hit {x - 4.0f, rect.y + 4.0f, w + 8.0f, rect.h - 8.0f};
    const bool hot = ui.hovered(hit);
    if(hot) fill(renderer, hit, theme().hoverBg);
    text.draw(label, x, baseline, hot ? theme().text : theme().muted, style);
    ui.crumbs.emplace_back(hit, trail[i]);
    x += w + 8.0f;
    text.draw("/", x, baseline, theme().dim, style);
    x += static_cast<float>(text.width("/", style)) + 8.0f;
  }
  if(note && x < limit) {
    drawNoteIcon(renderer, text, note->icon, {x, rect.y + 6.0f, 16.0f, 16.0f}, theme().dim);
    x += 20.0f;
    text.draw(ellipsizeToWidth(text, note->title, static_cast<int>(limit - x), style), x, baseline, theme().text, style);
  }

  if(note) {
    // A filled star reads as "kept"; the outline is an offer.
    ui.favoriteButton = {right - 34.0f, rect.y + 4.0f, 26.0f, rect.h - 8.0f};
    const bool pinned = ui.state.favorite(note->id);
    ui.offerTooltip(ui.favoriteButton, pinned ? "Remove from favorites" : "Add to favorites");
    if(ui.hovered(ui.favoriteButton)) fill(renderer, ui.favoriteButton, theme().hoverBg);
    text.draw(pinned ? "\xe2\x98\x85" : "\xe2\x98\x86", ui.favoriteButton.x + 6.0f, baseline,
              pinned ? theme().accent : theme().dim, style);
  }
}

}

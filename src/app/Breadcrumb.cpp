#include "app/Breadcrumb.h"

#include "app/Chrome.h"
#include "app/Notes.h"
#include "app/Shell.h"

#include "ui/Metrics.h"

#include "ui/TagColors.h"
#include "ui/Theme.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::TextRenderer;
using ui::ellipsizeToWidth;
using ui::fill;
using ui::hLine;
using ui::theme;

// The favourite star's target, and the icon in front of the note's own crumb.
// Both were written out inline, and the star's was written out twice -- once
// as its own left edge and once as the reserve the breadcrumb stops at.
constexpr float kFavoriteWidth = 26.0f;
constexpr float kCrumbIconSize = 14.0f;

// The star *mark*, which is not the size of the star's *target*.
//
// `drawStarGlyph` fills whatever box it is handed, and it was handed the whole
// 26x24 hit rect -- so the mark grew to fill a target sized for a pointer and
// ended up the largest thing in a 26px strip, competing with the note's own
// name rather than annotating it. A hit area is sized for the hand and a glyph
// for the eye; the tab strip already keeps those apart (`kTabCloseReserve`
// against `tabCloseHitRect`), and this is the same split.
//
// A point under the crumb icon beside it: a five-pointed star reads larger than
// a document glyph in the same box, so matching the box would not match the
// weight.
// Eleven, not thirteen: `drawStarGlyph` now takes the largest whole radius its
// field allows, so thirteen would draw a thirteen-pixel star where thirteen
// used to draw an eleven-pixel one. The mark keeps the weight this constant was
// tuned for.
constexpr float kFavoriteGlyphSize = 11.0f;

// What a note's tags ask for beside its name.
//
// They were a row of chips in the page header, above the note's first line: a
// band of ground and a coloured dot per tag, ahead of the note's own text. The
// dots say the same thing in eight pixels, in the same colours the sidebar puts
// on every row carrying the tag, on the band that already names the note.
float tagDotsWidth(std::size_t tagCount) {
  if(tagCount == 0) return 0.0f;
  const std::size_t shown = std::min(tagCount, ui::kMaxTagDots);
  return static_cast<float>(shown) * (ui::kTagDotSize + ui::kTagDotGap) + ui::kSpace1;
}

void drawTagDots(SDL_Renderer* renderer, UiRuntime& ui, const std::vector<std::string>& tags,
                 float x, Rect band) {
  if(tags.empty()) return;
  const auto& colors = ui.state.workspace().tagColors;
  const std::size_t shown = std::min(tags.size(), ui::kMaxTagDots);
  const float y = std::round(band.y + (band.h - ui::kTagDotSize) / 2.0f);
  for(std::size_t i = 0; i < shown; ++i) {
    const Rect dot {std::round(x), y, ui::kTagDotSize, ui::kTagDotSize};
    // Past the cap the last dot stands for the tags that did not fit, so it is
    // drawn in the muted ink rather than in any one tag's colour -- a marker,
    // not a tag -- and its tooltip names them. The sidebar's rows do the same,
    // for the same reason: hiding tags without saying so would be worse than
    // drawing no dots at all.
    const bool overflow = shown < tags.size() && i + 1 == shown;
    if(overflow) {
      std::string rest;
      for(std::size_t t = i; t < tags.size(); ++t) rest += (rest.empty() ? "" : ", ") + tags[t];
      ui::drawTagDot(renderer, dot, theme().textMuted);
      ui.pointer.offerTooltip(dot, rest);
    } else {
      ui::drawTagDot(renderer, dot, ui::tagColor(colors, tags[i]));
      ui.pointer.offerTooltip(dot, tags[i]);
    }
    x += ui::kTagDotSize + ui::kTagDotGap;
  }
}

}

void drawBreadcrumb(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui.chrome.crumbs.clear();
  ui.chrome.favoriteButton = {};
  if(ui::empty(rect)) return;
  fill(renderer, rect, theme().chromeBackground);
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h, theme().border);

  const ui::TextStyle style = ui::chromeStyle();
  const auto note = ui.state.catalog().isOpen() ? ui.state.catalog().findNote(ui.state.selection().noteId)
                                          : std::nullopt;
  // The same rule the status bar and every row in the shell uses. This was
  // `rect.y + max(4, (rect.h - line) / 2)`, unrounded and with a floor of its
  // own, so the one strip of chrome at the top of the window centred its text
  // by a slightly different arithmetic from the one at the bottom -- which is
  // exactly what `ui::textTop` exists to stop.
  const float baseline = ui::textTop(rect, text, style);

  // Whatever the star needs, so the trail cannot run under it. It was a bare
  // 44 against a star placed at `right - 34` with a width of 26, which is two
  // numbers that have to be kept in step by hand.
  const float limit = rect.x + rect.w - kFavoriteWidth - ui::kSpace2;
  float x = rect.x + ui::kSidebarInset;

  // Every crumb down to the note's own folder, root first.
  std::vector<std::filesystem::path> trail {{}};
  if(note) {
    std::filesystem::path walk;
    for(const auto& part : note->folder) {
      walk /= part;
      trail.push_back(walk);
    }
  }
  if(!ui.state.catalog().isOpen()) trail.clear();
  for(std::size_t i = 0; i < trail.size() && x < limit; ++i) {
    const auto label = trail[i].empty() ? ui.state.catalog().root().filename().generic_string()
                                        : trail[i].filename().generic_string();
    const float w = static_cast<float>(text.width(label, style));
    const Rect hit {x - ui::kSpace1, rect.y + 1.0f, w + ui::kSpace2, rect.h - 2.0f};
    const bool hot = ui.pointer.over(hit);
    if(hot) fill(renderer, hit, theme().rowHighlight);
    text.draw(label, x, baseline, hot ? theme().textPrimary : theme().textMuted, style);
    ui.chrome.crumbs.emplace_back(hit, trail[i]);
    x += w + ui::kSpace2;
    text.draw("/", x, baseline, theme().textDisabled, style);
    x += static_cast<float>(text.width("/", style)) + ui::kSpace2;
  }
  if(note && x < limit) {
    drawNoteIcon(renderer, note->icon,
                 {x, std::round(rect.y + (rect.h - kCrumbIconSize) / 2.0f), kCrumbIconSize,
                  kCrumbIconSize},
                 theme().textMuted);
    x += kCrumbIconSize + ui::kSpace1;
    // The dots are reserved for before the title is trimmed, so a long name is
    // ellipsized rather than pushing its own tags off the band.
    const float tagRoom = tagDotsWidth(note->tags.size());
    const auto title = ellipsizeToWidth(text, note->title, static_cast<int>(limit - x - tagRoom),
                                        style);
    text.draw(title, x, baseline, theme().textPrimary, style);
    x += static_cast<float>(text.width(title, style)) + ui::kSpace2;
    drawTagDots(renderer, ui, note->tags, x, rect);
  }

  if(!note) return;
  // A filled star reads as "kept"; the outline is an offer.
  ui.chrome.favoriteButton = {rect.x + rect.w - kFavoriteWidth - ui::kSpace2, rect.y + 1.0f,
                       kFavoriteWidth, rect.h - 2.0f};
  const bool pinned = ui.state.workspace().isFavorite(note->id);
  ui.pointer.offerTooltip(ui.chrome.favoriteButton, pinned ? "Remove from favorites" : "Add to favorites");
  if(ui.pointer.over(ui.chrome.favoriteButton)) fill(renderer, ui.chrome.favoriteButton, theme().rowHighlight);
  // The mark centred in the target rather than filling it. See
  // `kFavoriteGlyphSize`.
  ui::drawStarGlyph(renderer,
                    {std::round(ui.chrome.favoriteButton.x + (ui.chrome.favoriteButton.w - kFavoriteGlyphSize) / 2.0f),
                     std::round(ui.chrome.favoriteButton.y + (ui.chrome.favoriteButton.h - kFavoriteGlyphSize) / 2.0f),
                     kFavoriteGlyphSize, kFavoriteGlyphSize},
                    pinned, pinned ? theme().accent : theme().textMuted);
}

bool handleBreadcrumbClick(UiRuntime& ui, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  if(ui::contains(ui.chrome.favoriteButton, x, y)) {
    const auto noteId = ui.state.selection().noteId;
    ui.status = ui.state.editWorkspace().toggleFavorite(noteId) ? "Added to favorites" : "Removed from favorites";
    return true;
  }
  for(const auto& [crumb, folder] : ui.chrome.crumbs) {
    if(!ui::contains(crumb, x, y)) continue;
    if(!leaveOpenNote(ui)) return true;
    showFolder(ui, folder);
    ui.fields.search.reset();
    selectNoteAt(ui, 0);
    return true;
  }
  // The band's own empty space swallows the press rather than letting it reach
  // the page underneath, which is a different surface entirely.
  return true;
}

bool breadcrumbHasControlAt(const UiRuntime& ui, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  if(ui::contains(ui.chrome.favoriteButton, x, y)) return true;
  for(const auto& [crumb, folder] : ui.chrome.crumbs) {
    (void)folder;
    if(ui::contains(crumb, x, y)) return true;
  }
  return false;
}

}

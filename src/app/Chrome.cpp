#include "app/Chrome.h"

#include "app/Shell.h"

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

}

void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  // Three anchors and the way to the rest. The line used to name a dozen keys
  // and be truncated before it finished; every one of them is in F1 now, which
  // can hold them all and be searched.
  std::string help = std::string(paneModeName(ui.state.workspace().paneMode())) +
    "   " + ui::keysFor(ui::ActionId::GoToNote) + " Go to note" +
    "   " + ui::keysFor(ui::ActionId::CommandPalette) + " Commands" +
    "   " + ui::keysFor(ui::ActionId::Shortcuts) + " Shortcuts";
  if(ui.focus == FocusArea::Search) help = "Search all: " + ui.search.text() + "    Enter open  Esc clear";
  if(ui.focus == FocusArea::Find) help = "Find in note: " + ui.find.text() + "    Esc close";
  fill(renderer, {rect.x + 12, rect.y + 7, 6, 6}, ui.editor.dirty() ? theme().warn : theme().accent);
  text.draw(ui::ellipsize(help, 100), rect.x + 28, rect.y + 6, theme().muted);
  if(!ui.status.empty()) {
    const auto message = ui::ellipsize(ui.status, 72);
    Rect pill {std::max(rect.x + 12, rect.x + rect.w - static_cast<float>(text.width(message)) - 30), rect.y + 3, static_cast<float>(text.width(message)) + 18, 22};
    fill(renderer, pill, theme().surface);
    stroke(renderer, pill, theme().hairline);
    text.draw(message, pill.x + 9, rect.y + 6, theme().muted);
  }
}

// The trail of folders down to the open note. Clicking a crumb selects that
// folder, which is the shortest way back up from anywhere in the library.
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

void drawBreadcrumbs(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui.crumbs.clear();
  ui.favoriteButton = {};
  fill(renderer, rect, theme().editorBg);
  hLine(renderer, rect.x, rect.x + rect.w, rect.y + rect.h - 1.0f, theme().hairline);
  const ui::TextStyle style {ui::FontFamily::Sans, false, false, ui::type().small};
  const auto note = ui.state.findNote(ui.state.selection().noteId);

  const float baseline = rect.y + std::max(4.0f, (rect.h - static_cast<float>(text.lineHeight(style))) / 2.0f);
  float x = rect.x + 20.0f;
  const float limit = rect.x + rect.w - 44.0f;

  // Every crumb down to the note's own folder, root first.
  std::vector<std::filesystem::path> trail {{}};
  if(note) {
    std::filesystem::path walk;
    for(const auto& part : note->path.lexically_relative(ui.state.libraryRoot()).parent_path()) {
      walk /= part;
      trail.push_back(walk);
    }
  }
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
    ui.favoriteButton = {rect.x + rect.w - 34.0f, rect.y + 4.0f, 26.0f, rect.h - 8.0f};
    const bool pinned = ui.state.favorite(note->id);
    ui.offerTooltip(ui.favoriteButton, pinned ? "Remove from favorites" : "Add to favorites");
    if(ui.hovered(ui.favoriteButton)) fill(renderer, ui.favoriteButton, theme().hoverBg);
    text.draw(pinned ? "\xe2\x98\x85" : "\xe2\x98\x86", ui.favoriteButton.x + 6.0f, baseline,
              pinned ? theme().accent : theme().dim, style);
  }
}

}

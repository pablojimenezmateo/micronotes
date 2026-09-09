#include "app/RightPanel.h"

#include "app/EditorBlocks.h"
#include "app/Notes.h"
#include "app/Shell.h"
#include "app/SidebarModel.h"

#include "core/perf/PerformanceCounters.h"

#include "ui/Metrics.h"
#include "ui/Outline.h"
#include "ui/TagColors.h"
#include "ui/Theme.h"
#include "doc/WikiLink.h"
#include "ui/Glyphs.h"
#include "ui/Painter.h"
#include "ui/Scrollbar.h"
#include "ui/Widgets.h"
#include "ui/ClipGuard.h"

#include <algorithm>
#include <string>
#include <vector>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::theme;

constexpr float kHeaderHeight = 34.0f;
// Rows hold a line of UI text and a line grows with the reader's text size, so
// the pitch is the taller of a floor and what the text actually needs. A fixed
// 24 was a row that clipped its own text at the large size.
constexpr float kRowMinHeight = 24.0f;
constexpr float kBacklinkMinHeight = 44.0f;
// Clear space under the last row, and therefore the height the list loses out
// of the panel. It was folded into the ceiling as `contentHeight + kSpace2 -
// list.h`, which says the same thing about the content instead of about the
// viewport and is the reason this panel and the sidebar looked like they were
// computing two different ceilings.
constexpr float kListBottomPadding = ui::kSpace2;
// The panel's own inset, from the shell's spacing scale rather than from a
// number local to this file. It used to be 14, which is neither of the two
// insets the sidebar beside it uses.
constexpr float kPadX = ui::kSpace3;
constexpr float kIndentStep = ui::kSpace3;

// A row's own rect inside the list, inset the way the sidebar insets its rows.
// It was the full width of the panel here, so a selected heading in the outline
// was a square band running from the panel's left hairline to under the
// scrollbar, while a selected note in the sidebar beside it was an inset
// rounded fill -- two lists of rows, two shapes of selection, for the same
// "this one".
Rect rowRect(Rect list, float y, float height) {
  return {list.x + ui::kSpace2, y, std::max(0.0f, list.w - ui::kSpace2 * 2.0f), height};
}

// The tab a click lands on, and where each one is drawn. One geometry, read by
// both the paint and the hit test, so a tab cannot be painted off its own
// target.
Rect tabRect(Rect rect, int index, int count) {
  const float width = (rect.w - kPadX * 2.0f) / static_cast<float>(std::max(count, 1));
  return {rect.x + kPadX + width * static_cast<float>(index), rect.y + ui::kSpace1 + 2.0f, width,
          kHeaderHeight - 12.0f};
}

// Everything below the tabs: the scrolling list, which is the only thing the
// rows are placed in and hit-tested against.
Rect listRect(Rect rect) {
  return {rect.x, rect.y + kHeaderHeight, rect.w, std::max(0.0f, rect.h - kHeaderHeight)};
}

float rowPitch(const ui::TextRenderer& text, const ui::TextStyle& style) {
  return std::max(kRowMinHeight, static_cast<float>(text.lineHeight(style)) + ui::kSpace1);
}

const char* viewLabel(ui::RightPanelView view) {
  switch(view) {
    case ui::RightPanelView::Outline: return "Outline";
    case ui::RightPanelView::Backlinks: return "Links";
    case ui::RightPanelView::Tags: return "Tags";
  }
  return "";
}

constexpr ui::RightPanelView kViews[] = {ui::RightPanelView::Outline, ui::RightPanelView::Backlinks,
                                        ui::RightPanelView::Tags};

// The rows are a fixed pitch from the top of the list, so the row for entry `i`
// is arithmetic. It used to be a `vector<PanelRow>` built for every heading in
// the note and then walked until the first one fell off the bottom -- an
// allocation per frame to address the twenty rows a panel can show.
Rect outlineRowRect(Rect rect, std::size_t index, float pitch, int scroll) {
  const Rect list = listRect(rect);
  return rowRect(list, list.y + ui::kSpace1 + static_cast<float>(index) * pitch - static_cast<float>(scroll),
                 pitch);
}

// How far the panel can be scrolled, given everything the current view would
// draw. Recorded on the runtime by the draw so a wheel event can clamp against
// what was actually painted rather than laying the panel out a second time --
// the same arrangement `PageView` has, and the reason the panel and the wheel
// cannot disagree about where the bottom is.
void setMaxScroll(UiRuntime& ui, Rect rect, float contentHeight) {
  const Rect list = listRect(rect);
  ui.rightPanel.rect = rect;
  ui.rightPanel.list.setContent(list.h - kListBottomPadding, contentHeight);
}

}

// The outline of the open buffer, rebuilt only when the buffer has moved.
//
// "Only when the buffer has moved" is every keystroke, which is what made the
// rebuild worth caring about rather than a once-per-note cost: it was a block
// scan of the whole note per typed character -- 226 us on a 200 KB note, of
// which 194 us was the scan, against the ~14 us that keystroke's own layout
// update costs. The panel is on by default, so this was the largest thing a
// keystroke did.
//
// The partition comes from the live page now, through the same `editorBlocks`
// borrow the block edits take: the layout splices it during its own update, and
// the editor's revision is what proves it describes this buffer. When there is
// nothing to borrow -- the raw pane, the first keystroke after a note opens --
// it scans, which is what this always did.
//
// This is also why `drawApp` draws the right panel *after* the content. Ahead of
// it, the panel asked for the partition of a revision the layout had not reached
// yet, and the borrow missed every single time: the answer would have been last
// frame's, and `blocksAt` correctly refuses to hand that over. Moving the call
// is the whole difference between the borrow working and being decoration, and
// nothing about the paint depends on the order -- the shell's surfaces are
// disjoint rects and the tooltip and overlays are resolved after all of them.
const std::vector<ui::OutlineEntry>& outlineFor(UiRuntime& ui) {
  const std::uint64_t revision = ui.editor.revision();
  if(const auto* entries = ui.rightPanel.outline.get(revision)) {
    perf::addCounter(perf::CounterId::RightPanelOutlineReused);
    return *entries;
  }
  perf::addCounter(perf::CounterId::RightPanelOutlineBuilds);
  auto& entries = ui.rightPanel.outline.rebuild(revision);
  const doc::BlockSpan blocks = editorBlocks(ui);
  perf::addCounter(blocks.empty() ? perf::CounterId::RightPanelOutlineScans
                                  : perf::CounterId::RightPanelOutlineBlocksBorrowed);
  ui::outlineInto(ui.editor.text(), blocks, &entries);
  return entries;
}

namespace {

// The two views that come from the library rather than from the buffer. Both are
// filled together because both turn on the same key, and asking for either is
// what says the note or the library has moved.
const RightPanelState::LibraryViews& libraryViews(UiRuntime& ui) {
  const NoteRevision key {ui.state.selection().noteId, ui.state.revision()};
  if(const auto* views = ui.rightPanel.library.get(key)) {
    perf::addCounter(perf::CounterId::RightPanelLibraryReused);
    return *views;
  }
  perf::addCounter(perf::CounterId::RightPanelLibraryBuilds);
  auto& views = ui.rightPanel.library.rebuild(key);
  views.backlinks.clear();
  views.tags.clear();
  if(key.noteId.empty() || !ui.state.hasLibrary()) return views;
  views.backlinks = ui.state.backlinksToSelected();
  // From the open-note record rather than the file. This used to read and parse
  // the whole note -- for a row of chips -- on every library revision, and the
  // revision moves on every save.
  views.tags = ui.state.openNote().metadata.tags;
  return views;
}

}

void drawRightPanel(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui, Rect rect) {
  ui::fill(renderer, rect, theme().surfaceBackground);
  ui::fill(renderer, {rect.x, rect.y, 1.0f, rect.h}, theme().border);
  ui::ClipGuard clip(renderer, rect);

  const auto& workspace = ui.state.workspace();
  const ui::TextStyle tabStyle = ui::chromeStyle();
  const ui::TextStyle rowStyle = ui::chromeStyle();

  const int tabCount = static_cast<int>(std::size(kViews));
  for(int i = 0; i < tabCount; ++i) {
    const Rect tab = tabRect(rect, i, tabCount);
    const bool active = workspace.rightPanelView == kViews[i];
    const bool hot = ui::contains(tab, ui.pointer.x, ui.pointer.y);
    // The active mode takes the chrome's raised ground and an accent rule along
    // its foot, pointing at the list it heads. Hover takes the row highlight
    // alone -- the two used to take the same fill, which made the panel's
    // header say nothing about which of the three views was showing.
    if(active) {
      ui::fill(renderer, tab, theme().chromeActive);
      ui::fill(renderer, {tab.x, tab.y + tab.h - ui::kRowAccentWidth, tab.w, ui::kRowAccentWidth},
               theme().accent);
    } else if(hot) {
      ui::fill(renderer, tab, theme().rowHighlight);
    }
    const auto label = viewLabel(kViews[i]);
    const float labelX = tab.x + (tab.w - static_cast<float>(text.width(label, tabStyle))) / 2.0f;
    text.draw(label, labelX, ui::textTop(tab, text, tabStyle),
              active ? theme().chromeActiveText : hot ? theme().textPrimary : theme().textMuted,
              tabStyle);
  }
  ui::hLine(renderer, rect.x, rect.x + rect.w, rect.y + kHeaderHeight - 1.0f, theme().border);

  const Rect list = listRect(rect);
  ui.rightPanel.rebaseScroll(ui.state.workspace().rightPanelView, ui.state.selection().noteId);
  // An empty view has nothing to scroll, and the message says so where the rows
  // would have been.
  const auto empty = [&](std::string_view title, std::string_view detail, std::string_view keys = {}) {
    // The message's height is measured, not assumed, so a three-line empty
    // state at the large text size scrolls like every other view in this panel
    // instead of running off the bottom of it. Drawn at the standing scroll,
    // which the previous frame clamped, and the height reported afterwards --
    // the same order every list here uses.
    ui::ClipGuard clip(renderer, list);
    const float used = ui::drawEmptyMessage(text, title, detail, list.x,
                                            list.y - static_cast<float>(ui.rightPanel.list.scroll()), list.w, keys);
    setMaxScroll(ui, rect, used);
  };

  if(ui.state.selection().noteId.empty()) {
    empty("Nothing open", "Open a note to see what is in it.");
    return;
  }

  // Every view scrolls inside the list, so every view is clipped to it: a row
  // half off the bottom is cut at the edge rather than drawn over the tabs.
  ui::ClipGuard listClip(renderer, list);
  const int scroll = ui.rightPanel.list.scroll();

  if(workspace.rightPanelView == ui::RightPanelView::Outline) {
    const auto& entries = outlineFor(ui);
    if(entries.empty()) {
      empty("No headings", "Headings in this note show up here.");
      return;
    }
    const float pitch = rowPitch(text, rowStyle);
    setMaxScroll(ui, rect, static_cast<float>(entries.size()) * pitch);
    // Which entry the caret is in is not memoised: it moves with the caret
    // rather than with the buffer, and it is a walk of the headings rather than
    // of the note.
    const auto current = ui::outlineEntryAt(entries, ui.editor.cursor());
    // The band of rows the list can show, by arithmetic rather than by walking
    // from the first entry: a note with a thousand headings costs the same as
    // one with twenty.
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, static_cast<float>(scroll) - ui::kSpace1) / pitch);
    for(std::size_t i = first; i < entries.size(); ++i) {
      const Rect row = outlineRowRect(rect, i, pitch, scroll);
      if(row.y > list.y + list.h) break;
      const auto& entry = entries[i];
      const bool here = i == current;
      const bool hot = ui::contains(row, ui.pointer.x, ui.pointer.y);
      ui::drawRow(renderer, row, here, hot);
      const float x = row.x + kPadX + static_cast<float>(entry.depth) * kIndentStep;
      // A top-level heading carries the note's structure and reads as the
      // strong row; anything nested under it is support.
      const auto colour = here ? theme().textPrimary : (entry.depth == 0 ? theme().textSecondary : theme().textMuted);
      text.draw(ui::ellipsizeToWidth(text, entry.text, static_cast<int>(row.x + row.w - x - ui::kSpace2), rowStyle),
                x, ui::textTop(row, text, rowStyle), colour, rowStyle);
    }
    ui::drawVerticalScrollbar(renderer, list, scroll, ui.rightPanel.list.maxScroll());
    return;
  }

  const auto& views = libraryViews(ui);
  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    const auto& backlinks = views.backlinks;
    if(backlinks.empty()) {
      empty("Nothing links here",
            "Write [[the title of this note]] in another note and it will show up.");
      return;
    }
    const ui::TextStyle lineStyle = ui::chromeSmallStyle();
    // Two lines and the air around them, which at the large text size is more
    // than the 44 this row used to be nailed to.
    const float pitch = std::max(kBacklinkMinHeight,
                                 static_cast<float>(text.lineHeight(rowStyle) + text.lineHeight(lineStyle)) + ui::kSpace2);
    setMaxScroll(ui, rect, static_cast<float>(backlinks.size()) * pitch);
    float y = list.y + ui::kSpace1 - static_cast<float>(scroll);
    ui.rightPanel.backlinkRows.clear();
    for(const auto& link : backlinks) {
      if(y + pitch >= list.y && y <= list.y + list.h) {
        const Rect row = rowRect(list, y, pitch);
        ui::drawRow(renderer, row, false, ui::contains(row, ui.pointer.x, ui.pointer.y));
        const int room = static_cast<int>(row.w - kPadX * 2.0f);
        // Two lines centred in the row together, rather than dropped a fixed
        // two pixels into it: the pair grows with the reader's text size and
        // the row's floor does not.
        const float pairHeight = static_cast<float>(text.lineHeight(rowStyle) + text.lineHeight(lineStyle));
        const float titleY = std::round(y + std::max(0.0f, (pitch - pairHeight) / 2.0f));
        text.draw(ui::ellipsizeToWidth(text, link.title, room, rowStyle),
                  row.x + kPadX, titleY, theme().textPrimary, rowStyle);
        // The line the link was written on, which is the whole difference
        // between a list of titles and a reason to click one.
        // The line as it reads, not as it is written: the source line is what
        // the index stores, and it carries the `[[brackets]]` of the very link
        // that put this row here.
        text.draw(ui::ellipsizeToWidth(text, doc::plainWikiText(link.line), room, lineStyle),
                  row.x + kPadX, titleY + static_cast<float>(text.lineHeight(rowStyle)),
                  theme().textMuted, lineStyle);
        ui.rightPanel.backlinkRows.push_back({row, link.id});
      }
      y += pitch;
    }
    ui::drawVerticalScrollbar(renderer, list, scroll, ui.rightPanel.list.maxScroll());
    return;
  }

  const auto& tags = views.tags;
  if(tags.empty()) {
    empty("No tags", "This note carries none yet.", ui::keysFor(ui::ActionId::EditTags) + "  edit tags");
    return;
  }
  // Rows, not chips.
  //
  // These were bordered chips with no hover and no click handler -- they looked
  // exactly like the tag chips in the page header, which *are* a read-only
  // property display, while sitting in a panel list where every other row is
  // something you click. The sidebar's TAGS section is the same object with the
  // same affordance, so this is the same row: `#tag`, dim until it is the
  // filter in force, and a click sets that filter.
  const float pitch = rowPitch(text, rowStyle);
  setMaxScroll(ui, rect, static_cast<float>(tags.size()) * pitch);
  const std::string& activeTag = ui.state.selection().tag;
  float y = list.y + ui::kSpace1 - static_cast<float>(scroll);
  ui.rightPanel.tagRows.clear();
  for(const auto& tag : tags) {
    if(y + pitch >= list.y && y <= list.y + list.h) {
      const Rect row = rowRect(list, y, pitch);
      const bool selected = activeTag == tag;
      ui::drawRow(renderer, row, selected, ui::contains(row, ui.pointer.x, ui.pointer.y));
      // The tag's colour, and no `#`. Exactly the sidebar's tag row: this is
      // the same object with the same affordance, so it has to be the same mark
      // -- and a dot beside the name is what says "tag" now.
      ui::drawTagDot(renderer,
                     {row.x + kPadX, std::round(row.y + (row.h - kTagDotSize) / 2.0f),
                      kTagDotSize, kTagDotSize},
                     ui::tagColor(ui.state.workspace().tagColors, tag));
      const float labelX = row.x + kPadX + kTagDotSize + ui::kTreeLabelGap;
      text.draw(ui::ellipsizeToWidth(text, tag, static_cast<int>(row.x + row.w - labelX - kPadX), rowStyle),
                labelX, ui::textTop(row, text, rowStyle),
                selected ? theme().textPrimary : theme().textSecondary, rowStyle);
      ui.rightPanel.tagRows.push_back({row, tag});
    }
    y += pitch;
  }
  ui::drawVerticalScrollbar(renderer, list, scroll, ui.rightPanel.list.maxScroll());
}

// Non-const because the outline is cached lazily behind `outlineFor`, which is
// the same reason the click handler is.
bool rightPanelHasControlAt(UiRuntime& ui, const ui::TextRenderer& text, Rect rect,
                            float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  const auto& workspace = ui.state.workspace();
  const int tabCount = static_cast<int>(std::size(kViews));
  for(int i = 0; i < tabCount; ++i) {
    if(ui::contains(tabRect(rect, i, tabCount), x, y)) return true;
  }
  const Rect list = listRect(rect);
  if(const auto bar = ui::scrollbarGeometry(list, ui.rightPanel.list.scroll(), ui.rightPanel.list.maxScroll());
     bar && ui::contains(ui::scrollbarHitRect(bar->thumb), x, y)) {
    return true;
  }
  // The row lists the draw recorded, which is exactly what the click walks --
  // a cursor derived from anything else would promise clicks that miss.
  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    for(const auto& row : ui.rightPanel.backlinkRows) {
      if(ui::contains(row.rect, x, y)) return true;
    }
    return false;
  }
  if(workspace.rightPanelView == ui::RightPanelView::Tags) {
    for(const auto& row : ui.rightPanel.tagRows) {
      if(ui::contains(row.rect, x, y)) return true;
    }
    return false;
  }
  if(workspace.rightPanelView != ui::RightPanelView::Outline) return false;
  const auto& entries = outlineFor(ui);
  const ui::TextStyle rowStyle = ui::chromeStyle();
  const float pitch = rowPitch(text, rowStyle);
  const float offset = y - (list.y + ui::kSpace1) + static_cast<float>(ui.rightPanel.list.scroll());
  if(offset < 0.0f) return false;
  const auto index = static_cast<std::size_t>(offset / pitch);
  return index < entries.size() &&
         ui::contains(outlineRowRect(rect, index, pitch, ui.rightPanel.list.scroll()), x, y);
}

bool handleRightPanelClick(UiRuntime& ui, const ui::TextRenderer& text, Rect rect, float x, float y) {
  if(!ui::contains(rect, x, y)) return false;
  auto& workspace = ui.state.editWorkspace();
  const int tabCount = static_cast<int>(std::size(kViews));
  for(int i = 0; i < tabCount; ++i) {
    if(!ui::contains(tabRect(rect, i, tabCount), x, y)) continue;
    workspace.rightPanelView = kViews[i];
    return true;
  }
  // A click on the scrollbar is a click on the scrollbar, wherever the rows
  // under it happen to fall.
  const Rect list = listRect(rect);
  if(const auto bar = ui::scrollbarGeometry(list, ui.rightPanel.list.scroll(), ui.rightPanel.list.maxScroll());
     bar && ui::contains(ui::scrollbarHitRect(bar->track), x, y)) {
    return true;
  }
  if(workspace.rightPanelView == ui::RightPanelView::Backlinks) {
    for(const auto& row : ui.rightPanel.backlinkRows) {
      if(!ui::contains(row.rect, x, y)) continue;
      selectNoteById(ui, row.id);
      return true;
    }
    return true;
  }
  if(workspace.rightPanelView == ui::RightPanelView::Tags) {
    for(const auto& row : ui.rightPanel.tagRows) {
      if(!ui::contains(row.rect, x, y)) continue;
      selectTag(ui, row.id);
      return true;
    }
    return true;
  }
  if(workspace.rightPanelView != ui::RightPanelView::Outline) return true;
  const auto& entries = outlineFor(ui);
  // The rows are a fixed pitch, so the one under the pointer is arithmetic
  // rather than a walk of every heading in the note. The pitch is the drawn
  // one, and the scroll is the drawn one, or a click lands on the row that
  // would have been there before the list was scrolled.
  const ui::TextStyle rowStyle = ui::chromeStyle();
  const float pitch = rowPitch(text, rowStyle);
  const float offset = y - (list.y + ui::kSpace1) + static_cast<float>(ui.rightPanel.list.scroll());
  if(offset >= 0.0f) {
    const auto index = static_cast<std::size_t>(offset / pitch);
    if(index < entries.size() && ui::contains(outlineRowRect(rect, index, pitch, ui.rightPanel.list.scroll()), x, y)) {
      // Clicking a heading is a way of scrolling to it, so the caret goes to its
      // text rather than to the marker in front of it.
      ui.editor.moveCursor(entries[index].offset);
      ui.revealEditorCursor = true;
      ui.focus = FocusArea::Editor;
      return true;
    }
  }
  // The panel swallows clicks that land on its own background, or a click meant
  // for a row would fall through to the page behind it.
  return true;
}


// Showing and hiding a panel. The status line names what moved, because the
// window can be rearranged by a key that gives no other sign it did anything.
void togglePanel(UiRuntime& ui, bool ui::WorkspaceModel::*panel, std::string_view name) {
  auto& workspace = ui.state.editWorkspace();
  const bool showing = !(workspace.*panel);
  if(!workspace.togglePanel(panel)) {
    ui.status = "The last panel stays open";
    return;
  }
  ui.status = std::string(name) + (showing ? " shown" : " hidden");
}

void cycleRightPanel(UiRuntime& ui) {
  auto& workspace = ui.state.editWorkspace();
  switch(workspace.rightPanelView) {
    case ui::RightPanelView::Outline: workspace.rightPanelView = ui::RightPanelView::Backlinks; break;
    case ui::RightPanelView::Backlinks: workspace.rightPanelView = ui::RightPanelView::Tags; break;
    case ui::RightPanelView::Tags: workspace.rightPanelView = ui::RightPanelView::Outline; break;
  }
  // Cycling what the panel shows is also a way of asking for it.
  workspace.rightPanelVisible = true;
  ui.status = std::string(ui::rightPanelViewName(workspace.rightPanelView));
}

}

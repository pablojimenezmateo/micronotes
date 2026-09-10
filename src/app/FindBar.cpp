#include "app/FindBar.h"

#include "app/Clipboard.h"
#include "app/Fields.h"
#include "app/Shell.h"

#include "core/perf/Perf.h"
#include "core/util/TextSearch.h"

#include "ui/Actions.h"
#include "ui/Fonts.h"
#include "ui/Glyphs.h"
#include "ui/Metrics.h"
#include "ui/Painter.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace micronotes::app {
namespace {

using ui::Rect;
using ui::TextRenderer;
using ui::theme;

// There is no regex toggle, though ../microide's widget has one and this is
// otherwise its port.
//
// A regex over a Markdown note searches the *source*: the reader looking at a
// heading sees `An Idea` and the pattern has to match `## An Idea`, and a
// pattern that spans a line break spans one the reader never typed. That is a
// useful tool in a code editor, where the source is what is on screen, and a
// puzzle here, where it is not. The two toggles that survive mean the same thing
// in both applications, and the slot is left rather than filled with something
// that would need its own explanation of what it is matching against.

// What the count column is sized to hold, at whatever the reader's text size is.
//
// A fixed column rather than one measured from the string actually shown:
// the count changes on every keystroke, and a column that changes with it moves
// the three buttons to its right out from under the pointer while the reader is
// aiming at them.
constexpr std::string_view kWidestCount = "8888 of 8888";
constexpr std::string_view kNoResults = "No results";

float countColumnWidth(const TextRenderer& text, const ui::TextStyle& style) {
  return static_cast<float>(std::max(text.width(kWidestCount, style), text.width(kNoResults, style))) +
         ui::kSpace2;
}

// Where the reader is, as the offset a step forwards or backwards is measured
// from. The selection's *start*, so that stepping off a match the reader is
// sitting on goes to the neighbour rather than back onto itself.
std::size_t searchSeed(const UiRuntime& ui) {
  return ui.editor.hasSelection() ? ui.editor.selectionStart() : ui.editor.cursor();
}

}

// Selects the active match in the buffer and asks every pane showing the note
// to scroll to it.
//
// Not in the anonymous namespace: typing in the query box has to reveal too --
// find-as-you-type that finds without showing you is a match count -- and the
// field's text change lands in `app/Fields.h`'s one router rather than here.
//
// Selecting rather than only scrolling, for two reasons. It is what marks the
// match on the reading page -- it draws the buffer's selection, so the one the
// reader is on reads as picked out from the rest without a second treatment.
// And it is what lets Esc-then-copy work: the match you walked to is the
// selection you end up with.
void revealFindMatch(UiRuntime& ui) {
  if(!ui.find.hasMatches()) return;
  const util::TextMatch match = ui.find.matches[ui.find.active];
  ui.editor.selectRange(match.start, match.end);
  // The raw pane watches this; the reading pane has no caret of its own and
  // watches `revealViewerSelection` instead.
  ui.revealEditorCursor = true;
  ui.revealViewerSelection = true;
}

namespace {

void drawToggle(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect box,
                std::string_view label, bool active, std::string tooltip) {
  ui::drawButton(renderer, text, box, label, true, ui.pointer.over(box),
                 active ? ui::ButtonTone::Accent : ui::ButtonTone::Neutral);
  ui.pointer.offerTooltip(box, std::move(tooltip));
}

// The three icon buttons. Neutral rather than a tone per button: a close cross
// in the warning colour reads as "this deletes something", and closing a find
// bar deletes nothing.
void drawIconButton(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect box,
                    bool enabled, std::string tooltip) {
  ui::drawButton(renderer, text, box, "", enabled, enabled && ui.pointer.over(box));
  if(enabled) ui.pointer.offerTooltip(box, std::move(tooltip));
}

SDL_Color iconInk(bool enabled) {
  return enabled ? theme().textPrimary : theme().textDisabled;
}

}

FindBarLayout findBarLayout(Rect content, bool showing, const TextRenderer& text) {
  FindBarLayout layout;
  if(!showing || content.w <= 0.0f || content.h <= 0.0f) return layout;

  const ui::TextStyle style = ui::chromeStyle();
  // A floor, not a height: the row holds a text field, and text grows with the
  // reader's size setting while chrome does not.
  const float row = std::max(ui::kFindBarRowHeight, static_cast<float>(text.lineHeight(style)) + ui::kSpace1);
  const float available = std::max(ui::kFindBarMinWidth, content.w - 2.0f * ui::kFindBarMargin);
  const float width = std::min(ui::kFindBarMaxWidth, available);
  const float height = 2.0f * ui::kFindBarPad + row;
  // Clear of the page's scrollbar lane at the trailing edge, whether or not one
  // is showing this frame. A bar that shifts sideways when the note grows long
  // enough to scroll is a bar whose buttons move for a reason the reader cannot
  // see, so the lane is reserved unconditionally.
  const float rightInset = ui::kFindBarMargin + ui::kScrollbarThickness + ui::kScrollbarInset;
  layout.bar = {std::round(content.x + content.w - width - rightInset),
                std::round(content.y + ui::kFindBarMargin), std::round(width), std::round(height)};

  const float rowY = layout.bar.y + ui::kFindBarPad;
  const auto button = [&](float x) { return Rect {x, rowY, ui::kFindBarButton, row}; };

  // Right to left: close, next, previous, the count, then the toggles -- so
  // slot 0 stays next to the field however many toggles there are.
  layout.close = button(layout.bar.x + layout.bar.w - ui::kFindBarPad - ui::kFindBarButton);
  layout.next = button(layout.close.x - ui::kFindBarButtonGap - ui::kFindBarButton);
  layout.previous = button(layout.next.x - ui::kFindBarButtonGap - ui::kFindBarButton);
  const float countWidth = countColumnWidth(text, style);
  layout.count = {layout.previous.x - ui::kFindBarButtonGap - countWidth, rowY, countWidth, row};

  float x = layout.count.x;
  for(std::size_t index = kFindToggleCount; index-- > 0;) {
    x -= ui::kFindBarButtonGap + ui::kFindBarButton;
    layout.toggles[index] = button(x);
  }

  const float fieldX = layout.bar.x + ui::kFindBarPad;
  layout.field = {fieldX, rowY, std::max(ui::kFindBarMinField, x - ui::kFindBarButtonGap - fieldX), row};
  return layout;
}

FindBarLayout findBarLayout(const UiRuntime& ui, Rect content, const TextRenderer& text) {
  // The note as well as the flag. With nothing open the frame draws an empty
  // state over the content pane and never reaches the bar, and a layout that
  // said otherwise would leave the press router claiming a strip of a surface
  // nobody painted.
  return findBarLayout(content, ui.find.open && !ui.state.selection().noteId.empty(), text);
}

std::string findMatchCountText(const UiRuntime& ui) {
  if(!ui.find.open || ui.fields.find.text().empty()) return {};
  if(!ui.find.hasMatches()) return std::string(kNoResults);
  const std::string total = std::to_string(ui.find.matches.size());
  return std::to_string(ui.find.active + 1) + " of " + (ui.find.truncated ? total + "+" : total);
}

void drawFindBar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect content) {
  const FindBarLayout layout = findBarLayout(ui, content, text);
  if(ui::empty(layout.bar)) return;
  const perf::ScopeTimer timer("shell.find_bar");

  // A card with no title band, which is what a floating strip of controls is.
  // No backdrop: the note underneath stays readable and editable, which is the
  // whole difference between a find bar and a find dialog.
  (void)ui::drawTitledCard(renderer, layout.bar, 0.0f);

  const bool focused = ui.focus == FocusArea::Find;
  ui::drawTextFieldFrame(renderer, layout.field, focused);
  const Rect fieldText {layout.field.x + ui::kSpace2, ui::textTop(layout.field, text, ui::chromeStyle()),
                        std::max(1.0f, layout.field.w - ui::kSpace2 * 2.0f),
                        static_cast<float>(text.lineHeight(ui::chromeStyle()))};
  drawTextField(renderer, text, ui, ui.fields.find, fieldText, focused, "Find in this note");

  drawToggle(renderer, text, ui, layout.toggles[static_cast<std::size_t>(FindToggle::MatchCase)],
             "Aa", ui.find.options.matchCase, "Match case  " + ui::keysFor(ui::ActionId::FindMatchCase));
  drawToggle(renderer, text, ui, layout.toggles[static_cast<std::size_t>(FindToggle::WholeWord)],
             "ab", ui.find.options.wholeWord, "Whole word  " + ui::keysFor(ui::ActionId::FindWholeWord));

  const std::string count = findMatchCountText(ui);
  if(!count.empty()) {
    const ui::TextStyle style = ui::chromeStyle();
    const float width = static_cast<float>(text.width(count, style));
    text.draw(count, std::round(layout.count.x + (layout.count.w - width) / 2.0f),
              ui::textTop(layout.count, text, style),
              ui.find.hasMatches() ? theme().chromeText : theme().textMuted, style);
    if(ui.find.truncated) {
      ui.pointer.offerTooltip(layout.count,
                              "More than " + std::to_string(util::kMaxMatches) +
                                " matches; only the first are listed");
    }
  }

  const bool steppable = ui.find.hasMatches();
  drawIconButton(renderer, text, ui, layout.previous, steppable, "Previous match  " + ui::keysFor(ui::ActionId::FindPrevious));
  drawIconButton(renderer, text, ui, layout.next, steppable, "Next match  " + ui::keysFor(ui::ActionId::FindNext));
  drawIconButton(renderer, text, ui, layout.close, true, "Close  (Esc)");
  ui::drawArrowGlyph(renderer, layout.previous, ui::ArrowDirection::Up, iconInk(steppable));
  ui::drawArrowGlyph(renderer, layout.next, ui::ArrowDirection::Down, iconInk(steppable));
  ui::drawCloseGlyph(renderer, layout.close, theme().textPrimary);
}

bool pressFindBar(TextRenderer& text, UiRuntime& ui, Rect content, float x, float y, Uint8 button) {
  const FindBarLayout layout = findBarLayout(ui, content, text);
  if(ui::empty(layout.bar) || !ui::contains(layout.bar, x, y)) return false;
  if(button == SDL_BUTTON_MIDDLE) {
    // X11's second selection, pasted at the point pressed, like every other
    // text field in the shell. Swallowed even outside the query box: a middle
    // click on the bar must not reach the note it is floating over.
    if(ui::contains(layout.field, x, y)) {
      ui.focus = FocusArea::Find;
      ui.fields.find.editor.moveCursor(fieldOffsetAtX(text, ui.fields.find, layout.field, x));
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection"
                                                     : "No primary selection text";
    }
    return true;
  }
  if(button != SDL_BUTTON_LEFT) return true;

  if(ui::contains(layout.close, x, y)) {
    closeFindInNote(ui);
    return true;
  }
  if(ui::contains(layout.next, x, y)) {
    moveFindMatch(ui, 1);
    return true;
  }
  if(ui::contains(layout.previous, x, y)) {
    moveFindMatch(ui, -1);
    return true;
  }
  for(std::size_t index = 0; index < kFindToggleCount; ++index) {
    if(!ui::contains(layout.toggles[index], x, y)) continue;
    toggleFindOption(ui, static_cast<FindToggle>(index));
    return true;
  }
  if(ui::contains(layout.field, x, y)) {
    ui.focus = FocusArea::Find;
    const std::size_t offset = fieldOffsetAtX(text, ui.fields.find, layout.field, x);
    ui.fields.find.editor.moveCursor(offset);
    // The same drag the sidebar's search box answers: press, move, and the run
    // between the two is selected. See `TextFields::drawnRect`.
    ui.fieldSelect.active = true;
    ui.fieldSelect.anchor = offset;
    return true;
  }
  // The card's own ground. Swallowed rather than passed on: a press that misses
  // a button by a pixel must not land in the note behind the bar and move the
  // caret out from under the search.
  ui.focus = FocusArea::Find;
  return true;
}

void openFindInNote(UiRuntime& ui) {
  // Nothing to search. The key reaches `performCommand` straight from the
  // binding table, which -- unlike the palette and the menu -- does not consult
  // `ActionSpec::needsNote`, so this is where the answer has to be.
  if(ui.state.selection().noteId.empty()) return;
  // A selection in the note becomes the query. Looking for "the thing I just
  // highlighted" is what the key is reached for after, and re-typing it is the
  // step nobody wants; a selection spanning a line is not a needle anybody
  // meant, so those are left alone.
  const bool fromSelection = ui.editor.hasSelection() &&
                             ui.editor.selectedText().find('\n') == std::string::npos;
  if(fromSelection) ui.fields.find.beginWith(ui.editor.selectedText());
  else ui.fields.find.editor.selectAll();
  ui.find.open = true;
  ui.focus = FocusArea::Find;
  refreshFindMatches(ui);
  revealFindMatch(ui);
}

bool openFindFromSearch(UiRuntime& ui) {
  if(ui.state.selection().noteId.empty()) return false;
  const std::string query = ui.fields.search.text();
  if(query.empty()) return false;
  // Only when the note's *text* actually holds the query. A library search runs
  // over titles as well -- and under the Title scope over nothing else -- so a
  // result can be a note whose name matched and whose body never mentions the
  // word. Handing that reader a focused find bar reading "No results" puts a
  // card over the note they asked to read and swallows the next thing they
  // type. ../microide has no such case: a grep result is a line in the file.
  const std::string_view body = ui.editor.text();
  if(util::findFrom(body, query, 0, ui.find.options) >= body.size()) return false;

  ui.fields.find.beginWith(query);
  ui.find.open = true;
  ui.focus = FocusArea::Find;
  // No `forget` first: the buffer was just replaced, so the memo key's revision
  // has moved and the refresh below rescans rather than trusting the offsets
  // the previous note left behind.
  refreshFindMatches(ui);
  revealFindMatch(ui);
  return true;
}

void closeFindInNote(UiRuntime& ui) {
  ui.find.open = false;
  ui.find.forget();
  ui.fields.find.reset();
  // Back to the note, on the match last walked to: the selection stays, so
  // closing the bar leaves the reader where the search took them rather than
  // where they opened it.
  if(ui.focus == FocusArea::Find) ui.focus = FocusArea::Editor;
}

void refreshFindMatches(UiRuntime& ui) {
  FindState& find = ui.find;
  if(!find.open || ui.fields.find.text().empty()) {
    if(find.valid || find.hasMatches()) find.forget();
    return;
  }
  const FindMatchKey key {ui.editor.revision(), ui.fields.find.text(), find.options};
  if(find.valid && find.key == key) return;

  const perf::ScopeTimer timer("shell.find_scan");
  util::findAllInto(ui.editor.text(), key.needle, key.options, &find.matches, &find.truncated);
  find.key = key;
  find.valid = true;
  // The one at or after where the reader is, wrapping to the top. Searching
  // from the caret is what keeps find-as-you-type in the middle of a note where
  // it started, instead of jumping to the first hit in the document on every
  // character typed.
  find.active = util::matchAtOrAfter(find.matches, searchSeed(ui));
}

void moveFindMatch(UiRuntime& ui, int delta) {
  refreshFindMatches(ui);
  FindState& find = ui.find;
  if(!find.hasMatches() || delta == 0) return;
  const std::size_t seed = searchSeed(ui);
  find.active = delta > 0 ? util::matchAfter(find.matches, seed)
                          : util::matchBefore(find.matches, seed);
  revealFindMatch(ui);
}

bool handleFindBarKey(UiRuntime& ui, SDL_Keycode key, bool shift, bool alt) {
  if(!ui.find.open) return false;
  if(alt) {
    if(key == SDLK_C) {
      toggleFindOption(ui, FindToggle::MatchCase);
      return true;
    }
    if(key == SDLK_W) {
      toggleFindOption(ui, FindToggle::WholeWord);
      return true;
    }
    return false;
  }
  switch(key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      moveFindMatch(ui, shift ? -1 : 1);
      return true;
    case SDLK_DOWN:
      moveFindMatch(ui, 1);
      return true;
    case SDLK_UP:
      moveFindMatch(ui, -1);
      return true;
    default:
      return false;
  }
}

void toggleFindOption(UiRuntime& ui, FindToggle toggle) {
  switch(toggle) {
    case FindToggle::MatchCase:
      ui.find.options.matchCase = !ui.find.options.matchCase;
      break;
    case FindToggle::WholeWord:
      ui.find.options.wholeWord = !ui.find.options.wholeWord;
      break;
    case FindToggle::Count:
      return;
  }
  refreshFindMatches(ui);
  revealFindMatch(ui);
}

}

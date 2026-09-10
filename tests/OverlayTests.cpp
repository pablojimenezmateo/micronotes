#include "TestSupport.h"

#include "ui/Overlay.h"

#include <string>
#include <string_view>

namespace {

using micronotes::ui::Overlay;
using micronotes::ui::OverlayItem;
using micronotes::ui::OverlayKind;
using micronotes::ui::OverlayStack;

OverlayItem row(std::string id, std::string label) {
  return {std::move(id), std::move(label), {}, {}, true, false, false, false};
}

OverlayItem rule() {
  return {{}, {}, {}, {}, false, false, false, true};
}

OverlayItem heading(std::string label) {
  return {{}, std::move(label), {}, {}, false, false, false, false};
}

// A menu with a rule through the middle of it, which is the shape every context
// menu in the shell now has.
Overlay ruledMenu() {
  Overlay overlay;
  overlay.kind = OverlayKind::List;
  overlay.id = "ruled";
  overlay.items = {row("first", "First"), rule(), row("second", "Second"), row("third", "Third")};
  return overlay;
}

OverlayItem noteRow(std::string id, std::string label, std::string detail) {
  return {std::move(id), std::move(label), std::move(detail), {}, true, false, false, false};
}

// Pressing a key and reporting what came back, so a test reads as a sequence of
// keystrokes rather than as a sequence of out-parameters.
std::optional<micronotes::ui::OverlayResult> press(OverlayStack& stack, SDL_Keycode key,
                                                  bool shift = false) {
  bool handled = false;
  auto result = stack.handleKey(key, false, shift, handled);
  micronotes::tests::require(handled, "the overlay declined a key it should have taken");
  return result;
}

// Typing a query into a filterable list one character at a time, the way the
// shell feeds it.
void type(OverlayStack& stack, std::string_view query) {
  for(const char c : query) {
    const char text[2] = {c, '\0'};
    micronotes::tests::require(stack.handleText(text), "the overlay declined typed text");
  }
}

}

MICRONOTES_TEST(overlay_navigation_steps_over_a_separator) {
  OverlayStack stack;
  stack.open(ruledMenu());
  MICRONOTES_REQUIRE(stack.top()->highlighted == 0);
  press(stack, SDLK_DOWN);
  // Index 1 is the rule. Landing on it would leave a highlighted row that Enter
  // ignores, which reads as a menu that has stopped responding.
  MICRONOTES_REQUIRE(stack.top()->highlighted == 2);
  press(stack, SDLK_DOWN);
  MICRONOTES_REQUIRE(stack.top()->highlighted == 3);
}

MICRONOTES_TEST(overlay_navigation_steps_over_a_separator_going_back_as_well) {
  OverlayStack stack;
  stack.open(ruledMenu());
  press(stack, SDLK_UP);
  MICRONOTES_REQUIRE(stack.top()->highlighted == 3);
  press(stack, SDLK_UP);
  MICRONOTES_REQUIRE(stack.top()->highlighted == 2);
  press(stack, SDLK_UP);
  MICRONOTES_REQUIRE(stack.top()->highlighted == 0);
}

MICRONOTES_TEST(overlay_navigation_still_steps_over_a_disabled_heading) {
  // Section headings are listed as disabled rows. Separators did not exist when
  // that rule was written, and both have to be skipped.
  Overlay overlay;
  overlay.items = {heading("SECTION"), row("only", "Only")};
  OverlayStack stack;
  stack.open(std::move(overlay));
  MICRONOTES_REQUIRE(stack.top()->highlighted == 1);
  press(stack, SDLK_DOWN);
  MICRONOTES_REQUIRE(stack.top()->highlighted == 1);
}

MICRONOTES_TEST(a_menu_of_nothing_but_rules_does_not_hang) {
  Overlay overlay;
  overlay.items = {rule(), rule(), rule()};
  OverlayStack stack;
  stack.open(std::move(overlay));
  press(stack, SDLK_DOWN);
  press(stack, SDLK_UP);
  // Nothing to land on, and nothing to commit. The point is that both calls
  // return at all: the walk is bounded by the row count.
  MICRONOTES_REQUIRE(!press(stack, SDLK_RETURN).has_value());
}

MICRONOTES_TEST(a_separator_cannot_be_committed) {
  Overlay overlay;
  overlay.items = {rule(), row("real", "Real")};
  OverlayStack stack;
  stack.open(std::move(overlay));
  // The highlight opened on the only selectable row, not on the rule.
  MICRONOTES_REQUIRE(stack.top()->highlighted == 1);
  const auto result = press(stack, SDLK_RETURN);
  MICRONOTES_REQUIRE(result.has_value());
  MICRONOTES_REQUIRE(result->itemId == "real");
}

MICRONOTES_TEST(escape_closes_an_overlay_and_says_nothing_unless_asked) {
  Overlay overlay;
  overlay.items = {row("a", "A")};
  OverlayStack stack;
  stack.open(overlay);
  MICRONOTES_REQUIRE(!press(stack, SDLK_ESCAPE).has_value());
  MICRONOTES_REQUIRE(!stack.active());

  // An overlay that has been taking keystrokes reports the dismissal, so what
  // was typed can be put back where it came from.
  overlay.reportDismissal = true;
  overlay.filterable = true;
  stack.open(overlay);
  stack.handleText("Som");
  const auto dismissal = press(stack, SDLK_ESCAPE);
  MICRONOTES_REQUIRE(dismissal.has_value());
  MICRONOTES_REQUIRE(dismissal->itemId.empty());
  MICRONOTES_REQUIRE(dismissal->value == "Som");
}

// Every kind answers all five questions, and exactly one kind is neither a list
// nor a grid nor a confirmation -- the text prompt.
//
// The point of this test is what it does to a *sixth* kind: these are `if`
// chains over an enum rather than switches, so nothing in the compiler would
// notice one added without an answer here, and the arm it silently fell
// through would be whichever `else` happened to be last.
MICRONOTES_TEST(overlay_questions_cover_every_kind) {
  const OverlayKind kinds[] {OverlayKind::TextPrompt, OverlayKind::List, OverlayKind::Confirm,
                             OverlayKind::ColorPicker, OverlayKind::GlyphPicker};
  int shapes = 0;
  for(const OverlayKind kind : kinds) {
    Overlay overlay;
    overlay.kind = kind;
    // A kind is exactly one shape: a column of rows, a grid of cells, two
    // buttons, or a field on its own.
    const int is = static_cast<int>(overlay.hasRows()) + static_cast<int>(overlay.isGrid()) +
                   static_cast<int>(overlay.hasConfirmButtons());
    MICRONOTES_REQUIRE(is <= 1);
    if(is == 1) ++shapes;
    // Choosing an item is the two that have items to choose.
    MICRONOTES_REQUIRE(overlay.choosesAnItem() == (overlay.hasRows() || overlay.isGrid()));
    // And nothing but a prompt takes typing without being asked to filter.
    MICRONOTES_REQUIRE(overlay.takesTypedText() == (kind == OverlayKind::TextPrompt));
  }
  MICRONOTES_REQUIRE(shapes == 4);
}

// A list only takes typing once it is told to filter -- a context menu is a
// list, and typing into one would swallow the keystroke rather than run the
// command whose name was typed.
MICRONOTES_TEST(overlay_a_list_takes_typing_only_when_it_filters) {
  Overlay menu;
  menu.kind = OverlayKind::List;
  MICRONOTES_REQUIRE(!menu.takesTypedText());
  menu.filterable = true;
  MICRONOTES_REQUIRE(menu.takesTypedText());
}

// A note's own title is what a reader types at, so a title match ranks above a
// note found only through its folder -- however well that folder scores. Both
// used to come off one fuzzy scale, and a folder is a longer string with more
// separators to earn boundary bonuses on, so "pro" put a note called "Barometer
// log" above "Product roadmap" purely because it sat in projects/.
MICRONOTES_TEST(a_filtered_list_ranks_a_label_match_above_a_detail_match) {
  Overlay palette;
  palette.kind = OverlayKind::List;
  palette.id = "jump-note";
  palette.filterable = true;
  palette.items = {noteRow("barometer", "Barometer log", "projects"),
                   noteRow("roadmap", "Product roadmap", "work")};

  OverlayStack stack;
  stack.open(std::move(palette));
  type(stack, "pro");
  // Enter commits the highlighted row, which is the top of the filtered list.
  const auto result = press(stack, SDLK_RETURN);
  MICRONOTES_REQUIRE(result.has_value());
  MICRONOTES_REQUIRE(result->itemId == "roadmap");
}

// The tier is a tie-break between label and detail, not a replacement for the
// score: two notes matched on their titles still sort by how well they match.
MICRONOTES_TEST(a_filtered_list_still_scores_two_label_matches_against_each_other) {
  Overlay palette;
  palette.kind = OverlayKind::List;
  palette.id = "jump-note";
  palette.filterable = true;
  // Listed worst-first, so the order that comes back is the filter's doing and
  // not the order the items went in.
  palette.items = {noteRow("barometer", "Barometer log", ""),
                   noteRow("roadmap", "Roadmap meeting", "")};

  OverlayStack stack;
  stack.open(std::move(palette));
  type(stack, "rm");
  const auto result = press(stack, SDLK_RETURN);
  MICRONOTES_REQUIRE(result.has_value());
  MICRONOTES_REQUIRE(result->itemId == "roadmap");
}

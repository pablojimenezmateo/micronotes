#include "TestSupport.h"

#include "ui/Overlay.h"

#include <string>

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

// Pressing a key and reporting what came back, so a test reads as a sequence of
// keystrokes rather than as a sequence of out-parameters.
std::optional<micronotes::ui::OverlayResult> press(OverlayStack& stack, SDL_Keycode key,
                                                  bool shift = false) {
  bool handled = false;
  auto result = stack.handleKey(key, false, shift, handled);
  micronotes::tests::require(handled, "the overlay declined a key it should have taken");
  return result;
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

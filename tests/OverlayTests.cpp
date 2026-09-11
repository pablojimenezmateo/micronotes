#include "TestSupport.h"

#include "ui/Overlay.h"
#include "ui/OverlayStyle.h"
#include "ui/TextRenderer.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// --- the panel's geometry ----------------------------------------------------
//
// None of this had a test. `layoutFor` sizes the panel by *summing* the bands
// it is made of and then *walks* down those same bands placing rects into it,
// and the two were separate expressions naming the same eight things -- so a
// band in one and not the other is a panel whose box and whose contents
// disagree.
//
// Two mutations were used to find out what a test here has to check, and they
// fail differently:
//
//   * **A band missing from the sum** -- drop `hint` from `PanelBands::total`
//     -- makes the panel shorter than the walk needs, and containment catches
//     it: the confirm buttons land outside the panel, and the hint lands on
//     the last row.
//   * **A band too narrow for its text** -- size the swatch grid to its
//     swatches while its hint is wider -- is invisible to containment, and
//     that is the part worth knowing. Every band's rect is *derived* from the
//     panel's box (a hint is `width - padding * 2` wide, wherever the panel
//     ends up), so the rect is inside by construction and only the text
//     spills. The panel was the thing that was wrong.
//
// So there are three properties, and the second mutation is the reason the
// width one exists at all.

namespace {

bool inside(const micronotes::ui::Rect& outer, const micronotes::ui::Rect& inner) {
  // Half a pixel of slack on each edge: the placement rounds and the sum does
  // not, so an exact containment test would fail on rounding rather than on
  // anything a reader could see.
  const float slack = 0.51f;
  return inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
         inner.x + inner.w <= outer.x + outer.w + slack &&
         inner.y + inner.h <= outer.y + outer.h + slack;
}

// Strictly: touching edges are not an overlap, and a half pixel of rounding is
// not one either.
bool overlaps(const micronotes::ui::Rect& a, const micronotes::ui::Rect& b) {
  const float slack = 0.51f;
  return a.x + a.w > b.x + slack && b.x + b.w > a.x + slack && a.y + a.h > b.y + slack &&
         b.y + b.h > a.y + slack;
}

std::string describe(const micronotes::ui::Rect& r) {
  return "{" + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" +
         std::to_string(r.h) + "}";
}

// Every overlay shape the shell opens, so the property is checked against the
// kinds rather than against one convenient case.
std::vector<std::pair<std::string, Overlay>> everyOverlayShape() {
  std::vector<std::pair<std::string, Overlay>> shapes;

  shapes.emplace_back("a ruled context menu", ruledMenu());

  Overlay anchored = ruledMenu();
  anchored.anchored = true;
  anchored.anchorX = 900.0f;
  anchored.anchorY = 700.0f;
  anchored.maxRows = static_cast<int>(anchored.items.size());
  shapes.emplace_back("an anchored menu near the bottom right", std::move(anchored));

  Overlay titled = ruledMenu();
  titled.title = "A title long enough to need its own band";
  titled.hint = "Enter  choose        Esc  cancel";
  shapes.emplace_back("a titled menu with a hint", std::move(titled));

  Overlay palette;
  palette.kind = OverlayKind::List;
  palette.id = "palette";
  palette.filterable = true;
  palette.title = "Go to note";
  for(int i = 0; i < 200; ++i) {
    palette.items.push_back(row("note-" + std::to_string(i), "Note " + std::to_string(i)));
  }
  shapes.emplace_back("a filterable palette of two hundred rows", std::move(palette));

  Overlay prompt;
  prompt.kind = OverlayKind::TextPrompt;
  prompt.id = "rename";
  prompt.title = "Rename note";
  prompt.hint = "Enter  rename        Esc  cancel";
  shapes.emplace_back("a text prompt", std::move(prompt));

  Overlay confirm;
  confirm.kind = OverlayKind::Confirm;
  confirm.id = "delete-note";
  confirm.title = "Delete this note?";
  // The hint is the consequence and must be read before the buttons, which is
  // the one place the hint is not at the panel's foot.
  confirm.hint = "This cannot be undone.";
  confirm.confirmLabel = "Delete";
  shapes.emplace_back("a confirm with a consequence", std::move(confirm));

  Overlay grid;
  grid.kind = OverlayKind::ColorPicker;
  grid.id = "tag-color";
  grid.title = "work";
  // Wider than the swatches it sits under, which is the case that put a hint
  // outside its own panel.
  grid.hint = "Enter  choose        Esc  cancel";
  for(int i = 0; i < 12; ++i) grid.items.push_back(row(std::to_string(i), {}));
  shapes.emplace_back("a swatch grid whose hint outruns it", std::move(grid));

  Overlay glyphs;
  glyphs.kind = OverlayKind::GlyphPicker;
  glyphs.id = "icon";
  glyphs.title = "Set icon";
  for(int i = 0; i < 11; ++i) glyphs.items.push_back(row("g" + std::to_string(i), "Mark"));
  shapes.emplace_back("a glyph picker", std::move(glyphs));

  return shapes;
}

}

// Everything the layout places is inside the panel the layout declares.
//
// This is what catches a band missing from the height: the panel comes out
// short and the last things placed -- a confirm's buttons -- fall out of the
// bottom of it. Checked at window sizes from a phone-shaped sliver to a wide
// desktop, because a list is capped by the room left over and a short window
// is where the arithmetic is most likely to come apart.
MICRONOTES_TEST(an_overlay_panel_contains_everything_it_places) {
  micronotes::ui::TextRenderer text(nullptr);
  OverlayStack stack;
  const int sizes[][2] = {{1600, 1000}, {900, 600}, {640, 400}, {420, 300}, {2560, 1440}};

  for(auto& [what, shape] : everyOverlayShape()) {
    for(const auto& size : sizes) {
      Overlay copy = shape;
      const auto layout = stack.layoutFor(copy, text, size[0], size[1]);
      const std::string where =
        " (" + what + " at " + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ")";

      micronotes::tests::require(layout.panel.w > 0.0f && layout.panel.h > 0.0f,
                                 "the panel has no area" + where);
      micronotes::tests::require(layout.panel.x >= 0.0f && layout.panel.y >= 0.0f,
                                 "the panel starts off-window at " + describe(layout.panel) + where);
      micronotes::tests::require(layout.panel.x + layout.panel.w <= static_cast<float>(size[0]),
                                 "the panel runs past the right edge: " + describe(layout.panel) +
                                   where);

      const auto contains = [&](const micronotes::ui::Rect& r, const char* part) {
        if(r.w <= 0.0f && r.h <= 0.0f) return;  // a band this overlay does not have
        micronotes::tests::require(inside(layout.panel, r),
                                   std::string(part) + " " + describe(r) +
                                     " is outside its panel " + describe(layout.panel) + where);
      };
      contains(layout.title, "the title band");
      contains(layout.field, "the text field");
      contains(layout.hint, "the hint");
      for(const auto& item : layout.itemRects) contains(item, "an item");
    }
  }
}

// The rows a panel says it fitted are the rows it placed.
//
// These are two numbers in two places -- `RowStrip::shown`, which the
// scrolling clamps against, and the item rects the paint and the hit test walk
// -- and a panel that reports more than it drew scrolls past its own end.
//
// `shown` has a floor of one, deliberately: a list too tall for its pane still
// has to read as a list, so a pane always claims its first row even clipped.
// An overlay with no list at all -- a prompt, a confirm -- therefore reports
// one and places none, and that is the floor showing rather than a
// disagreement. Stated here rather than skipped, because a test that steps
// around a case it does not like stops describing the thing.
MICRONOTES_TEST(an_overlay_places_exactly_the_rows_it_says_it_fitted) {
  micronotes::ui::TextRenderer text(nullptr);
  OverlayStack stack;
  for(auto& [what, shape] : everyOverlayShape()) {
    Overlay copy = shape;
    if(copy.isGrid()) continue;  // a grid places every swatch, `shown` counts rows
    const auto layout = stack.layoutFor(copy, text, 900, 600);
    std::size_t rows = 0;
    for(const int index : layout.itemIndices) {
      if(index >= 0) ++rows;  // -1 and -2 are the confirm buttons
    }
    const auto shown = static_cast<std::size_t>(copy.rows.shown);
    const bool agrees = rows == shown || (rows == 0 && shown == 1);
    micronotes::tests::require(agrees, what + ": placed " + std::to_string(rows) +
                                         " rows but reported " + std::to_string(copy.rows.shown));
  }
}

// A band is wide enough for what it carries.
//
// The mutation containment cannot see: a swatch grid asks for the width of its
// swatches, its hint is wider than they are, and so the hint's rect -- the
// panel's width less its padding -- is narrower than the text drawn into it.
// The rect was inside the panel the whole time; the panel was the thing that
// was wrong, which is why this asks about the text rather than the box.
MICRONOTES_TEST(an_overlay_hint_gets_a_band_wide_enough_to_read) {
  micronotes::ui::TextRenderer text(nullptr);
  OverlayStack stack;
  for(auto& [what, shape] : everyOverlayShape()) {
    if(shape.hint.empty()) continue;
    Overlay copy = shape;
    const auto layout = stack.layoutFor(copy, text, 1600, 1000);
    const auto needed = static_cast<float>(text.width(copy.hint, micronotes::ui::hintFace()));
    micronotes::tests::require(layout.hint.w + 0.51f >= needed,
                               what + ": the hint needs " + std::to_string(needed) +
                                 "px and its band is " + std::to_string(layout.hint.w) + "px, so '" +
                                 copy.hint + "' is drawn outside the panel");
  }
}

// No two bands land on each other.
//
// Drop the hint from the height and the panel comes out shorter while the hint
// is still placed at `foot - hint`, so it lands on top of the last row. The
// containment test above catches that too, by a different symptom -- so this
// is the one that says *which* two things collided, which is the difference
// between a failure somebody can act on and one they have to reproduce.
//
// Item rects are excluded from each other on purpose: a swatch grid places its
// cells in columns, so two items sharing a row of the panel is the grid
// working rather than a collision.
MICRONOTES_TEST(an_overlay_places_no_two_bands_on_top_of_each_other) {
  micronotes::ui::TextRenderer text(nullptr);
  OverlayStack stack;
  const int sizes[][2] = {{1600, 1000}, {900, 600}, {640, 400}, {420, 300}};

  for(auto& [what, shape] : everyOverlayShape()) {
    for(const auto& size : sizes) {
      Overlay copy = shape;
      const auto layout = stack.layoutFor(copy, text, size[0], size[1]);
      const std::string where =
        " (" + what + " at " + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ")";

      struct Band {
        const char* name;
        micronotes::ui::Rect rect;
      };
      std::vector<Band> bands;
      const auto add = [&](const char* name, const micronotes::ui::Rect& r) {
        if(r.w > 0.0f && r.h > 0.0f) bands.push_back({name, r});
      };
      add("the title band", layout.title);
      add("the text field", layout.field);
      add("the hint", layout.hint);
      for(const auto& item : layout.itemRects) add("an item", item);

      for(std::size_t a = 0; a < bands.size(); ++a) {
        for(std::size_t b = a + 1; b < bands.size(); ++b) {
          if(std::string(bands[a].name) == "an item" && std::string(bands[b].name) == "an item") {
            continue;
          }
          micronotes::tests::require(!overlaps(bands[a].rect, bands[b].rect),
                                     std::string(bands[a].name) + " " + describe(bands[a].rect) +
                                       " lands on " + bands[b].name + " " +
                                       describe(bands[b].rect) + where);
        }
      }
    }
  }
}

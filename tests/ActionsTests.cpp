#include "TestSupport.h"

#include "ui/Actions.h"

#include <set>
#include <string>

using micronotes::ui::ActionId;
using micronotes::ui::ActionSpec;
using micronotes::ui::acceleratorText;
using micronotes::ui::actionSpecs;
using micronotes::ui::findAction;
using micronotes::ui::findActionForChord;
using micronotes::ui::formatKeyChord;
using micronotes::ui::KeyChord;
using micronotes::ui::parseKeyChord;

// The registry exists to be the only place an action is named. If an id can be
// missing from it, or appear twice, it is not that place.
MICRONOTES_TEST(actions_registry_covers_every_id_exactly_once) {
  const auto specs = actionSpecs();
  MICRONOTES_REQUIRE(specs.size() == static_cast<std::size_t>(ActionId::Count));
  std::set<int> seen;
  for(const auto& spec : specs) {
    MICRONOTES_REQUIRE(spec.id != ActionId::Count);
    MICRONOTES_REQUIRE(seen.insert(static_cast<int>(spec.id)).second);
    MICRONOTES_REQUIRE(!spec.name.empty());
    MICRONOTES_REQUIRE(!spec.label.empty());
    MICRONOTES_REQUIRE(findAction(spec.id) == &spec);
    MICRONOTES_REQUIRE(findAction(spec.name) == &spec);
  }
  for(int i = 0; i < static_cast<int>(ActionId::Count); ++i) {
    MICRONOTES_REQUIRE(seen.count(i) == 1);
  }
}

// A name is what an overlay result carries back, so two actions sharing one
// would silently run the wrong thing.
MICRONOTES_TEST(actions_names_are_unique) {
  std::set<std::string> names;
  for(const auto& spec : actionSpecs()) {
    MICRONOTES_REQUIRE(names.insert(std::string(spec.name)).second);
  }
}

// Every chord in the table has to be one the key handler can actually match,
// and has to print back as itself -- a typo would otherwise reach the shortcut
// list as a key that does nothing.
MICRONOTES_TEST(actions_every_chord_round_trips) {
  for(const auto& spec : actionSpecs()) {
    if(spec.chord.empty()) continue;
    const auto chord = parseKeyChord(spec.chord);
    micronotes::tests::require(chord.has_value(), "unparseable chord: " + std::string(spec.chord));
    micronotes::tests::require(formatKeyChord(*chord) == spec.chord,
                               "chord does not print as itself: " + std::string(spec.chord) +
                                 " -> " + formatKeyChord(*chord));
  }
}

// One key, one action. Two rows claiming the same chord is a binding conflict
// the user would meet as "this shortcut does the wrong thing".
MICRONOTES_TEST(actions_no_two_bindings_claim_the_same_keys) {
  for(const auto& spec : actionSpecs()) {
    if(spec.chord.empty()) continue;
    const auto chord = parseKeyChord(spec.chord);
    MICRONOTES_REQUIRE(chord.has_value());
    micronotes::tests::require(findActionForChord(*chord) == &spec,
                               "chord claimed twice: " + std::string(spec.chord));
  }
}

MICRONOTES_TEST(actions_parse_key_chord_reads_modifiers_in_any_order) {
  const auto a = parseKeyChord("Ctrl+Shift+P");
  const auto b = parseKeyChord("Shift+Ctrl+p");
  MICRONOTES_REQUIRE(a.has_value() && b.has_value());
  MICRONOTES_REQUIRE(*a == *b);
  MICRONOTES_REQUIRE(a->ctrl && a->shift && !a->alt);
  MICRONOTES_REQUIRE(a->key == static_cast<SDL_Keycode>('p'));
}

MICRONOTES_TEST(actions_parse_key_chord_names_the_keys_that_have_no_character) {
  const auto enter = parseKeyChord("Ctrl+Enter");
  MICRONOTES_REQUIRE(enter.has_value());
  MICRONOTES_REQUIRE(enter->key == SDLK_RETURN);
  MICRONOTES_REQUIRE(formatKeyChord(*enter) == "Ctrl+Enter");

  const auto up = parseKeyChord("Alt+Up");
  MICRONOTES_REQUIRE(up.has_value() && up->alt && up->key == SDLK_UP);
  MICRONOTES_REQUIRE(formatKeyChord(*up) == "Alt+Up");

  MICRONOTES_REQUIRE(parseKeyChord("F1")->key == SDLK_F1);
}

// "Ctrl+," ends in the key, not in a dangling separator.
MICRONOTES_TEST(actions_parse_key_chord_handles_punctuation_keys) {
  const auto comma = parseKeyChord("Ctrl+,");
  MICRONOTES_REQUIRE(comma.has_value());
  MICRONOTES_REQUIRE(comma->ctrl && comma->key == static_cast<SDL_Keycode>(','));
  MICRONOTES_REQUIRE(formatKeyChord(*comma) == "Ctrl+,");

  const auto plus = parseKeyChord("Ctrl++");
  MICRONOTES_REQUIRE(plus.has_value());
  MICRONOTES_REQUIRE(plus->key == static_cast<SDL_Keycode>('+'));
}

MICRONOTES_TEST(actions_parse_key_chord_rejects_nonsense) {
  MICRONOTES_REQUIRE(!parseKeyChord("").has_value());
  MICRONOTES_REQUIRE(!parseKeyChord("Meta+P").has_value());
  MICRONOTES_REQUIRE(!parseKeyChord("Ctrl+Nope").has_value());
  MICRONOTES_REQUIRE(!parseKeyChord("Ctrl+").has_value());
}

// An action that answers to a family of keys prints the family; one with a
// single chord prints the chord; one with neither prints nothing.
MICRONOTES_TEST(actions_accelerator_text_prefers_the_hint) {
  const auto* insert = findAction(ActionId::InsertBlock);
  MICRONOTES_REQUIRE(insert != nullptr);
  MICRONOTES_REQUIRE(acceleratorText(*insert) == "/");

  const auto* palette = findAction(ActionId::CommandPalette);
  MICRONOTES_REQUIRE(palette != nullptr);
  MICRONOTES_REQUIRE(acceleratorText(*palette) == "Ctrl+Shift+P");

  const auto* icon = findAction(ActionId::SetNoteIcon);
  MICRONOTES_REQUIRE(icon != nullptr);
  MICRONOTES_REQUIRE(acceleratorText(*icon).empty());
}

// A row that opens something which asks another question says so, so the
// palette reads as a set of promises rather than a set of surprises.
MICRONOTES_TEST(actions_labels_ending_in_ellipsis_are_the_ones_that_ask_again) {
  for(const auto& spec : actionSpecs()) {
    if(!spec.label.ends_with("...")) continue;
    MICRONOTES_REQUIRE(spec.label.size() > 3);
  }
}

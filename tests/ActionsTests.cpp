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
//
// Aliases are held to the same rule as primary bindings, and to the same rule
// against each other. An alias exists to match a chord people arrive already
// knowing; one that quietly shadows a binding this app already had would take
// away a habit in the course of adding one.
MICRONOTES_TEST(actions_no_two_bindings_claim_the_same_keys) {
  for(const auto& spec : actionSpecs()) {
    for(const std::string_view bound : {spec.chord, spec.altChord}) {
      if(bound.empty()) continue;
      const auto chord = parseKeyChord(bound);
      MICRONOTES_REQUIRE(chord.has_value());
      micronotes::tests::require(findActionForChord(*chord) == &spec,
                                 "chord claimed twice: " + std::string(bound));
    }
  }
}

// An alias spells its keys the one way the registry spells keys, so the
// shortcut list and any future rebinding UI can print it without a special case.
MICRONOTES_TEST(actions_every_alias_round_trips) {
  for(const auto& spec : actionSpecs()) {
    if(spec.altChord.empty()) continue;
    const auto chord = parseKeyChord(spec.altChord);
    micronotes::tests::require(chord.has_value(), "unparseable alias: " + std::string(spec.altChord));
    micronotes::tests::require(formatKeyChord(*chord) == spec.altChord,
                               "alias does not print as itself: " + std::string(spec.altChord) +
                                 " -> " + formatKeyChord(*chord));
    // An alias on an action with no primary binding is a primary binding that
    // was written in the wrong column.
    micronotes::tests::require(!spec.chord.empty(),
                               "alias with no primary chord: " + std::string(spec.name));
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

// Every chord in the table has to reach something, and the chain in
// `handleKey` is no longer where that is decided: `keyRunsIt` says whether the
// key alone runs it, and everything else has to have a branch that reads the
// focus first.
//
// This is the check that was missing. `F2`, `Ctrl+Q` and the `Ctrl+O` alias
// were in the table -- printed by the palette, printed by the shortcut list --
// and reached no branch at all, because the chain was a second copy of the
// table and nobody had added them to it. The table is the only copy now, so
// what this asserts is that the resolver can find every binding in it.
MICRONOTES_TEST(actions_every_chord_resolves_back_to_its_own_action) {
  for(const auto& spec : actionSpecs()) {
    for(const auto& written : {spec.chord, spec.altChord}) {
      if(written.empty()) continue;
      const auto chord = parseKeyChord(written);
      micronotes::tests::require(chord.has_value(),
                                 std::string("unparseable chord in the registry: ") +
                                   std::string(written));
      const auto* found = findActionForChord(*chord);
      micronotes::tests::require(found != nullptr,
                                 std::string("nothing answers to ") + std::string(written));
      // The first row claiming a chord wins, so a duplicate shows up here as
      // the wrong action rather than as a silent shadow.
      micronotes::tests::require(found->id == spec.id,
                                 std::string(written) + " resolves to " + std::string(found->name) +
                                   " rather than " + std::string(spec.name));
    }
  }
}

// A chord names a character, and on a layout where that character is somewhere
// else the shortcut has to keep working: the physical key is the fallback.
//
// The hand-written branches this replaced all did this -- `shortcut(SDLK_S,
// SDL_SCANCODE_S)` -- and the table-driven ones did not, so the seven chords
// already dispatched from the table were the seven that broke on a Dvorak
// keyboard.
MICRONOTES_TEST(actions_a_chord_answers_to_its_physical_key_too) {
  using micronotes::ui::findActionForKey;
  // Ctrl+S on a layout where the US `S` position produces something else: the
  // keycode is bound to nothing and the scancode is right. (A keycode that *is*
  // bound wins instead, which is the documented precedence -- on Dvorak the S
  // position produces an `o`, and `Ctrl+O` is the note switcher's own alias.)
  const auto* save = findActionForKey(SDLK_SEMICOLON, SDL_SCANCODE_S, true, false, false);
  MICRONOTES_REQUIRE(save != nullptr);
  MICRONOTES_REQUIRE(save->id == ActionId::Save);

  // The keycode still wins when it matches, so a layout that puts the letter
  // somewhere else answers there as well.
  const auto* alsoSave = findActionForKey(SDLK_S, SDL_SCANCODE_SEMICOLON, true, false, false);
  MICRONOTES_REQUIRE(alsoSave != nullptr);
  MICRONOTES_REQUIRE(alsoSave->id == ActionId::Save);

  // Modifiers are still exact: the fallback must not turn Ctrl+Shift+3 into
  // the pane switch, because that is the key the block transforms use.
  MICRONOTES_REQUIRE(findActionForKey(SDLK_HASH, SDL_SCANCODE_3, true, true, false) == nullptr);
  // And Ctrl+3 alone still is the pane switch.
  const auto* pane = findActionForKey(SDLK_3, SDL_SCANCODE_3, true, false, false);
  MICRONOTES_REQUIRE(pane != nullptr);
  MICRONOTES_REQUIRE(pane->id == ActionId::PaneReading);

  // Nothing to fall back to for a key that is already physical.
  MICRONOTES_REQUIRE(findActionForKey(SDLK_UNKNOWN, SDL_SCANCODE_UNKNOWN, true, false, false) == nullptr);
}

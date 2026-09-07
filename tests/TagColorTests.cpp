#include "TestSupport.h"

#include "ui/ColorMath.h"
#include "ui/TagColors.h"
#include "ui/Theme.h"

#include <set>
#include <string>

using micronotes::ui::contrast;
using micronotes::ui::defaultTagSwatch;
using micronotes::ui::kIncidentalContrast;
using micronotes::ui::kTagSwatchCount;
using micronotes::ui::setThemeMode;
using micronotes::ui::TagColors;
using micronotes::ui::tagColor;
using micronotes::ui::tagSwatch;
using micronotes::ui::theme;
using micronotes::ui::ThemeMode;

// A tag with no colour picked for it still has a colour, and the same one every
// time. Without that, a library's dots are all one shade until somebody has
// visited a picker twelve times -- which is a feature nobody would find, since
// the dots are what tells you the picker exists.
MICRONOTES_TEST(tag_colors_derive_a_stable_default_from_the_name) {
  MICRONOTES_REQUIRE(defaultTagSwatch("work") == defaultTagSwatch("work"));
  MICRONOTES_REQUIRE(defaultTagSwatch("work") != defaultTagSwatch("personal"));
  // In range for anything, including the empty name and a very long one.
  for(const char* name : {"", "a", "work", "personal", "a-very-long-tag-name-nobody-would-type",
                          "\xc3\xa9\xc3\xa8"}) {
    const int swatch = defaultTagSwatch(name);
    MICRONOTES_REQUIRE(swatch >= 0 && swatch < kTagSwatchCount);
  }

  // Pinned. The values are a function of the name and nothing else, which is
  // what makes them the same on two machines looking at the same library -- and
  // it is also why the hash is written out in `TagColors.cpp` instead of taken
  // from `util::hashBytes`, whose header says changing it is free. Changing it
  // is not free here: it would repaint every library in existence.
  MICRONOTES_REQUIRE(defaultTagSwatch("work") == 6);
  MICRONOTES_REQUIRE(defaultTagSwatch("personal") == 7);
  MICRONOTES_REQUIRE(defaultTagSwatch("markdown") == 2);

  // And the spread is real rather than nominal: a dozen ordinary tag names must
  // not all land on two or three swatches, or the dots stop distinguishing
  // anything. Six distinct out of twelve names is a low bar the derivation
  // should clear comfortably.
  const char* names[] = {"work", "personal", "archive", "fast", "local", "sqlite",
                         "markdown", "intro", "todo", "ideas", "reading", "meeting"};
  std::set<int> distinct;
  for(const char* name : names) distinct.insert(defaultTagSwatch(name));
  MICRONOTES_REQUIRE(distinct.size() >= 6);
}

// A picked colour beats the derived one, and clearing goes back to derived --
// which is a different thing from picking the colour the default happens to be.
MICRONOTES_TEST(tag_colors_remember_what_was_picked_and_only_that) {
  TagColors colors;
  MICRONOTES_REQUIRE(!colors.picked("work"));
  MICRONOTES_REQUIRE(colors.swatchOf("work") == defaultTagSwatch("work"));
  MICRONOTES_REQUIRE(colors.choices().empty());

  colors.set("work", 3);
  MICRONOTES_REQUIRE(colors.picked("work"));
  MICRONOTES_REQUIRE(colors.swatchOf("work") == 3);
  // Only choices are held, so a tag that has never been to the picker costs
  // nothing and follows the palette if it ever changes.
  MICRONOTES_REQUIRE(colors.choices().size() == 1);
  MICRONOTES_REQUIRE(colors.swatchOf("personal") == defaultTagSwatch("personal"));

  colors.clear("work");
  MICRONOTES_REQUIRE(!colors.picked("work"));
  MICRONOTES_REQUIRE(colors.swatchOf("work") == defaultTagSwatch("work"));
  MICRONOTES_REQUIRE(colors.choices().empty());

  // Picking the colour it already had is still a choice: it pins the tag
  // against a future change to the derivation, which is the whole difference.
  colors.set("work", defaultTagSwatch("work"));
  MICRONOTES_REQUIRE(colors.picked("work"));

  // An index from a state file written against a different palette wraps into
  // range rather than being stored out of it, so every reader agrees with
  // `tagSwatch`.
  colors.set("wrapped", kTagSwatchCount + 2);
  MICRONOTES_REQUIRE(colors.swatchOf("wrapped") == 2);
  colors.set("negative", -1);
  MICRONOTES_REQUIRE(colors.swatchOf("negative") >= 0);
  MICRONOTES_REQUIRE(colors.swatchOf("negative") < kTagSwatchCount);

  // An empty name is not a tag and cannot be given a colour.
  colors.set("", 4);
  MICRONOTES_REQUIRE(!colors.picked(""));

  colors.clearAll();
  MICRONOTES_REQUIRE(colors.choices().empty());
}

// A renamed tag keeps its colour. Without this, retagging a note repaints every
// dot that tag was showing, for no reason the reader could see.
MICRONOTES_TEST(tag_colors_follow_a_renamed_tag) {
  TagColors colors;
  colors.set("wrok", 5);
  colors.rename("wrok", "work");
  MICRONOTES_REQUIRE(!colors.picked("wrok"));
  MICRONOTES_REQUIRE(colors.picked("work"));
  MICRONOTES_REQUIRE(colors.swatchOf("work") == 5);

  // Renaming onto a tag that already had a colour is a merge, and the tag that
  // survives keeps what it looked like -- the reader is left with the colour
  // they can still see rather than one that has just vanished.
  colors.set("other", 9);
  colors.rename("work", "other");
  MICRONOTES_REQUIRE(colors.swatchOf("other") == 9);
  MICRONOTES_REQUIRE(!colors.picked("work"));

  // Renaming something with no colour is not an error and invents nothing.
  colors.rename("never-coloured", "still-not");
  MICRONOTES_REQUIRE(!colors.picked("still-not"));
}

// The reason a tag's colour is stored as an *index* and not as an RGB: the two
// palettes are different colours, and one pick has to read correctly in both.
// A stored RGB that was legible on the dark theme could be invisible on the
// light one, and nothing at draw time could recover which colour was meant.
MICRONOTES_TEST(tag_swatches_stay_visible_in_both_palettes) {
  for(const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
    setThemeMode(mode);
    const std::string which = mode == ThemeMode::Dark ? "dark" : "light";
    std::set<std::string> seen;
    for(int i = 0; i < kTagSwatchCount; ++i) {
      const SDL_Color swatch = tagSwatch(i);
      // Against the panel the dots are drawn on. A dot nobody can see is worse
      // than no dot: it is a mark that says the note has no tags.
      const float ratio = contrast(swatch, theme().surfaceBackground);
      micronotes::tests::require(
        ratio >= kIncidentalContrast - 0.05f,
        which + " theme: tag swatch " + std::to_string(i) + " is " + std::to_string(ratio) +
          ":1 against the panel -- a dot nobody can see reads as a note with no tags");
      MICRONOTES_REQUIRE(swatch.a == 255);
      // And twelve swatches have to be twelve colours, or the picker offers a
      // choice that does not distinguish anything.
      seen.insert(std::to_string(swatch.r) + "," + std::to_string(swatch.g) + "," +
                  std::to_string(swatch.b));
    }
    micronotes::tests::require(seen.size() == static_cast<std::size_t>(kTagSwatchCount),
                               which + " theme: only " + std::to_string(seen.size()) +
                                 " of the swatches are distinct colours");

    // An index out of range wraps rather than reading off the end of the
    // palette: it means a state file from a version with a different one, and a
    // wrong-but-legible colour beats a crash or an invisible dot.
    MICRONOTES_REQUIRE(tagSwatch(kTagSwatchCount).r == tagSwatch(0).r);
    MICRONOTES_REQUIRE(tagSwatch(-1).r == tagSwatch(kTagSwatchCount - 1).r);
    MICRONOTES_REQUIRE(tagSwatch(-kTagSwatchCount - 1).a == 255);
  }
  setThemeMode(ThemeMode::Dark);
}

// The one question the draw actually asks.
MICRONOTES_TEST(tag_color_resolves_a_name_to_a_colour) {
  setThemeMode(ThemeMode::Dark);
  TagColors colors;
  const SDL_Color derived = tagColor(colors, "work");
  MICRONOTES_REQUIRE(derived.r == tagSwatch(defaultTagSwatch("work")).r);
  colors.set("work", 1);
  const SDL_Color chosen = tagColor(colors, "work");
  MICRONOTES_REQUIRE(chosen.r == tagSwatch(1).r);
  MICRONOTES_REQUIRE(chosen.g == tagSwatch(1).g);
}

#pragma once

#include <SDL3/SDL_pixels.h>

#include "core/util/Hash.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace micronotes::ui {

// What colour a tag is.
//
// A tag is the one way of organising a library that cuts across the tree, so
// the sidebar can list a note under exactly one folder and under any number of
// tags -- but the tree row for that note said nothing at all about its tags,
// and the TAGS section said nothing about which notes carried them. A colour is
// what joins the two up: a dot at the trailing edge of a note's row, in the
// same colour as the tag's own row, and the connection is visible without
// reading anything.
//
// Stored as an **index into a palette**, not as a colour.
//
// That is the whole design decision here. A stored RGB would be a colour that
// was legible on the theme it was picked in and possibly illegible on the
// other -- a tag coloured near-white on the dark theme is a tag that vanishes
// on the light one, and no amount of correcting at draw time recovers which
// colour the reader meant. An index is a semantic choice, resolved to the
// palette in force, so one pick reads correctly in both. It is also what the
// rest of the shell does: `Theme` stores roles and not pixels.
//
// The picker therefore offers a fixed set of swatches rather than a colour
// wheel. That is a feature and not a shortcut: every swatch is one somebody
// checked against both grounds, so no pick can produce a dot nobody can see.

// How a tag dot is drawn, wherever one is drawn: the sidebar's note rows, the
// outline panel's tag list and the breadcrumb over the open note.
//
// Capped, because every one of those places is one line tall and a note with
// nine tags would push its own name out of the way. Past the cap the last dot
// is a marker rather than a tag, and whatever draws it names the ones that did
// not fit in a tooltip -- hiding them silently would be worse than drawing no
// dots at all.
inline constexpr std::size_t kMaxTagDots = 4;
inline constexpr float kTagDotSize = 7.0f;
inline constexpr float kTagDotGap = 4.0f;

// The swatches, and the order the picker lays them out in. Twelve is enough
// that a library's tags are told apart at a glance and few enough that the
// picker is one glance rather than a search.
inline constexpr int kTagSwatchCount = 12;

// The swatch at `index`, in the palette in force, held to the incidental
// contrast ratio against the panel it is drawn on so a dot is always visible.
//
// Wraps rather than refuses: an index out of range means a state file written
// by a version with a different palette, and a wrong-but-legible colour is a
// better answer there than a crash or an invisible dot.
SDL_Color tagSwatch(int index);

// The swatch a tag takes when nobody has picked one for it.
//
// Derived from the name so that tags are distinguishable the moment they exist
// -- a library whose dots are all grey until somebody visits a picker twelve
// times has a feature nobody will find. Stable across runs and across machines
// because it is a function of the name alone.
//
// Its own hash rather than `util::hashBytes`, deliberately: that one keys
// in-memory caches and its header says changing it is free. This one decides
// what colour a reader's tags are, so changing it would silently repaint a
// library, and it must stay put.
int defaultTagSwatch(std::string_view tag);

// The picked swatches, and nothing else.
//
// Only what somebody chose is held, so a tag that has never been to the picker
// costs nothing and follows `defaultTagSwatch` if the palette grows. Keyed with
// `std::less<>` so a lookup by `string_view` does not first allocate a string
// to look itself up by -- this is asked once per drawn dot per frame.
class TagColors {
public:
  // The swatch index for `tag`: what was picked, or the default.
  int swatchOf(std::string_view tag) const;
  // Whether anybody chose this one. The picker uses it to show which swatch is
  // in force, and the persistence to write only real choices.
  bool picked(std::string_view tag) const;

  void set(std::string tag, int swatch);
  // Back to the default, which is a different thing from picking the colour the
  // default happens to be: a cleared tag follows the palette again.
  void clear(std::string_view tag);
  void clearAll();

  // Round-trips through the ui state file.
  const std::map<std::string, int, std::less<>>& choices() const;

private:
  std::map<std::string, int, std::less<>> picked_;
};

// The colour a tag is drawn in, which is the only question the draw asks.
SDL_Color tagColor(const TagColors& colors, std::string_view tag);

}

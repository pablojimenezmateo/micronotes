#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// Whether a `[[target]]` names a note that exists. Asked once per wikilink per
// layout, so the library listing behind it is cached.
bool wikiLinkResolves(UiRuntime& ui, std::string_view target);

// Drops that cache and moves the revision the layout keys its blocks under.
// Both, always: dropping the cache without moving the revision leaves every
// standing block layout coloured by the old answer, and the two used to be one
// bare assignment repeated at six call sites.
void invalidateWikiNotes(UiRuntime& ui);

// Follows a `[[target]]`, creating the note when there is not one yet.
void openWikiLink(UiRuntime& ui, std::string_view target);

// The picker offered by the second `[` of a `[[`. `wikiStart` is the first one.
void openWikiMenu(UiRuntime& ui, std::size_t wikiStart);

// Finishes what the picker started: a chosen title becomes a whole link; an
// empty one means the reader escaped, and gets their brackets and their typing
// back rather than losing both.
void commitWikiMenu(UiRuntime& ui, const std::string& title, const std::string& typed);

}

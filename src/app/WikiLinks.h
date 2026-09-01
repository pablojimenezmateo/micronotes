#pragma once

#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// Whether a `[[target]]` names a note that exists. Asked once per wikilink per
// layout, so the library listing behind it is cached.
bool wikiLinkResolves(UiRuntime& ui, std::string_view target);

// Follows a `[[target]]`, creating the note when there is not one yet.
void openWikiLink(UiRuntime& ui, std::string_view target);

}

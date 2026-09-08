#pragma once

#include "library/LibraryIndex.h"

#include <string_view>

namespace micronotes::library {

// What the search field's scope badge says, and what clicking it does.
//
// Three pure functions of `library::SearchScope`, kept together because the
// badge, its tooltip and the click all have to name the same three scopes in
// the same order: a badge reading "T" beside a tooltip saying "titles and text"
// is worse than no badge, and the two used to be adjacent switch statements
// with nothing holding them together.

// One letter, because that is all the room the badge has.
std::string_view searchScopeLabel(library::SearchScope scope);

// The whole phrase, for the tooltip, because "A" tells nobody anything.
std::string_view searchScopeName(library::SearchScope scope);

// What the badge changes to when it is clicked. Cycles, so the scope the user
// wants is always at most two clicks away and no click is a dead end.
library::SearchScope nextSearchScope(library::SearchScope scope);

}

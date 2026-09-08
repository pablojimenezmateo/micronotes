#pragma once

#include "library/Organization.h"

#include <cstddef>
#include <string_view>
#include <vector>

// Which note a `[[target]]` names.
//
// Apart from `doc/WikiLink.h` because it is a different question with a
// different dependency: the syntax of a link is a fact about the text, and this
// is a search of the library. Keeping them together is what made the library
// layer include the UI layer, which made the two a cycle.
namespace micronotes::library {

// The note a `[[target]]` names, or npos.
//
// Resolution is by title, then by file name, each tried exactly and then
// case-insensitively, and finally as a library-relative path. **Never by the
// front-matter id.** An id is invisible in the file: a link keyed on one is
// unreadable in any other editor and unfixable by hand, which is the opposite
// of what "the notes are plain Markdown files you could have written yourself"
// promises.
//
// Ties go to the shortest path, so a note at the root beats a deeply filed one
// with the same name.
std::size_t resolveWikiLink(std::string_view target, const std::vector<NoteListItem>& notes);

}

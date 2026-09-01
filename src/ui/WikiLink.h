#pragma once

#include "library/Organization.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// A `[[target]]` split into the parts that mean different things.
struct WikiTarget {
  std::string note;     // what to look for in the library
  std::string heading;  // the `#fragment`, empty when there was none
};

WikiTarget splitWikiTarget(std::string_view target);

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
std::size_t resolveWikiLink(std::string_view target, const std::vector<library::NoteListItem>& notes);

// Rewrites every `[[old]]` in `source` to `[[new]]`, keeping any `|alias` and
// any `#heading` exactly as they were. Used after a rename, and only with the
// writer's say-so: it edits files they did not open.
std::string retargetWikiLinks(std::string_view source, std::string_view from, std::string_view to);

// Every wikilink target in a note, in order, with the line each appeared on.
// A backlink without the line it came from is a list of titles; with it, it is
// a reason to click.
struct WikiReference {
  std::string target;
  std::string line;
};

std::vector<WikiReference> wikiReferences(std::string_view source);

}

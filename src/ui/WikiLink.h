#pragma once

#include "library/Organization.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::ui {

// One `[[target]]`, or `[[target|what to call it here]]`, found in a run of
// plain text.
//
// `doc::InlineScan` already claims these on the live surface, from the note
// buffer with a mask over what other passes have taken. The reading pane has no
// such structure to work from -- md4c leaves `[[Some Note]]` as literal text,
// which is why the pane that exists for reading a note used to show the raw
// brackets and offer nothing to click. It needs the same rule applied to a bare
// string, so the rule lives here and both are the same rule.
struct WikiSpan {
  std::size_t start = 0;   // the first `[`
  std::size_t end = 0;     // one past the last `]`
  std::string target;      // what to resolve against the library
  std::string label;       // what to draw: the alias, or the target itself
};

// The first wikilink at or after `from`, or nothing. An unterminated `[[`, an
// empty `[[]]` and an alias with no target before the bar are all left as the
// literal text they are.
std::optional<WikiSpan> findWikiLink(std::string_view text, std::size_t from = 0);

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

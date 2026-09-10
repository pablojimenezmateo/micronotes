#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a `[[wikilink]]` says, as a question about the text alone.
//
// This was `ui/WikiLink.h`, and it did not belong there for a reason that was
// not a matter of taste: `library/LibraryIndex.cpp` needs `wikiReferences` to
// build the backlink table, so the library layer included the UI layer -- and
// the UI layer includes the library, so the two directories were a cycle. The
// syntax of a link is a fact about a Markdown document, which is what `doc/` is
// for, and `library` may depend on `doc`.
//
// What is *not* here is resolution: which note a target names is a question
// about the library, and it lives in `library/WikiResolve.h`.
namespace micronotes::doc {

// One `[[target]]`, or `[[target|what to call it here]]`, found in a run of
// plain text.
//
// `doc::InlineScan` already claims these on the page, from the note
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

// `text` with every `[[target]]` and `[[target|alias]]` replaced by the words
// it would be drawn as. For anywhere a line of a note is shown as a preview
// rather than rendered: the backlinks panel drew the raw source line, so a list
// of reasons to click a note was a list of `[[double brackets]]`.
std::string plainWikiText(std::string_view text);

// A `[[target]]` split into the parts that mean different things.
struct WikiTarget {
  std::string note;     // what to look for in the library
  std::string heading;  // the `#fragment`, empty when there was none
};

WikiTarget splitWikiTarget(std::string_view target);

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

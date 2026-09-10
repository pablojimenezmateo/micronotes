#pragma once

#include <cstddef>
#include <string_view>

namespace microcore::markdown {

// Where a bare `http://` or `https://` URL written straight into prose ends.
//
// Two pipelines have to answer that: `MarkdownParser`, which md4c hands text
// runs to, and the document scanner in `doc/InlineScan` that the page
// lays out from. They had better agree -- a URL that is a link in the reading
// view and plain text in the editor is the app appearing to forget -- so the
// rule is written once here rather than twice at either end.
struct BareUrl {
  // The URL itself, with the sentence punctuation it swallowed given back.
  // Zero when the text at `pos` does not begin a URL at all.
  std::size_t length = 0;
  // The whole unbroken run, punctuation included: where a caller resumes, and
  // what it emits as text after the link.
  std::size_t span = 0;
};

// A URL runs to the next space or angle bracket, and then hands back the
// punctuation a sentence left on the end of it: "see https://example.com/x.pdf."
// links to the PDF and leaves the full stop as text.
BareUrl bareUrlAt(std::string_view text, std::size_t pos);

}

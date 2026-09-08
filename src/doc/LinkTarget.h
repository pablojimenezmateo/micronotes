#pragma once

#include <string>
#include <string_view>

// What a Markdown link's target says, before anything looks for the thing it
// names.
//
// Three questions about a target string, together because a caller that
// follows a link has to ask all three in order and the answers are not
// independent: whether it leaves the machine at all, what it says once the
// escapes are undone, and -- for a `#fragment` -- which heading it points at.
//
// Here rather than in `ui/` because none of it draws or measures anything, and
// because `library/` asks the third one when it builds the heading index. A
// link's meaning is a property of the document.
namespace micronotes::doc {

// Whether the target names something on the network rather than in the
// library. Remote targets are handed to the desktop and never resolved against
// a note.
[[nodiscard]] bool isRemoteTarget(std::string_view target);

// The target with its escapes undone: percent-escapes and CommonMark's
// backslash form.
//
// Malformed escapes are *kept as written*. A lone `%` and a backslash before a
// non-punctuation byte are both legal characters in a file name on this
// platform, so decoding them into something else would turn a link that works
// into one that does not.
[[nodiscard]] std::string decodeLinkTarget(std::string_view target);

// The slug a `#fragment` matches: the heading's text, lowercased, with every
// run of non-alphanumeric bytes becoming a single dash. GitHub's rule, because
// it is the one people have already learnt.
[[nodiscard]] std::string headingAnchor(std::string_view value);

}

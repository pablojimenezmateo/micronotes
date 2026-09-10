#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

// Finding a literal needle in a buffer, addressed by byte offset.
//
// App-agnostic on purpose: it knows about bytes, ASCII case and word
// boundaries, and nothing about notes, Markdown or panes. The sibling
// `../microide` has the same engine over a line/column document
// (`workspace/WorkspaceTextSearch.h`); this is that engine for a buffer whose
// positions are already offsets, which is what `editor::MarkdownEditor` hands
// out.
//
// It exists because the same scan had been written three times in the shell and
// all three disagreed. The find bar counted matches with a `find` loop over the
// whole note on every keystroke; the note page kept its own cached list; and the
// raw pane re-scanned every visible line on every frame -- so the number in the
// status bar, the highlights on the page and the highlights in the raw pane were
// three independent answers to one question, and none of them could be told to
// match case or to stop mid-word.
namespace microcore::util {

// What the find bar's two toggles mean, threaded through every entry point so
// the count and the highlights cannot disagree about what was asked.
//
// `matchCase` false folds both sides (ASCII only -- see `core/util/StringUtil.h`
// for why the fold stops there). `wholeWord` is applied as a *filter* over the
// matches rather than as a rewrite of the needle, so it means the same thing
// however the needle is spelled.
struct SearchOptions {
  bool matchCase = false;
  bool wholeWord = false;

  friend bool operator==(const SearchOptions&, const SearchOptions&) = default;
};

// A match, as a half-open byte range. The end is carried rather than derived
// from the needle's length because a caller holding the list should not have to
// hold the needle as well to draw one.
struct TextMatch {
  std::size_t start = 0;
  std::size_t end = 0;

  friend bool operator==(const TextMatch&, const TextMatch&) = default;
};

// A byte a word can be made of: ASCII alphanumerics, the underscore, and every
// byte of every multi-byte code point.
//
// The last clause is what makes "whole word" usable on prose that is not
// English. Treating a continuation byte as a separator would make `naive` a
// whole-word match inside `naïve`, because the `ï` either side of a boundary is
// not ASCII. Taken from ../microide's `util::IsSearchWordByte`, where the same
// rule governs the terminal find bar and the in-file one.
inline constexpr bool isWordByte(char c) {
  const auto byte = static_cast<unsigned char>(c);
  return byte >= 0x80 || (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') || byte == '_';
}

// Whether [start, end) of `text` has a non-word byte on both sides -- the
// whole-word predicate, on its own so a caller filtering a list it already has
// does not have to re-derive it.
bool standsAlone(std::string_view text, std::size_t start, std::size_t end);

// The first match at or after `from`, or `text.size()` when there is none.
//
// `text.size()` rather than `npos` as the miss: every caller here compares
// against the buffer's length anyway, and a sentinel that is also a valid
// "past the end" offset removes the branch that mixing the two needs.
std::size_t findFrom(std::string_view text, std::string_view needle, std::size_t from,
                     SearchOptions options);

// Every match in `text`, in ascending order, non-overlapping.
//
// Non-overlapping is the same rule `std::string::find` in a loop with a step of
// the needle's length gives: `aa` in `aaa` is one match, not two. It is what the
// three scans this replaces already did, and what a reader stepping through
// matches expects -- two highlights over the same three characters is one
// highlight that looks broken.
//
// The list is capped at `kMaxMatches`. A one-character needle in a large note is
// otherwise a vector the size of the note, reassigned on every keystroke, and
// what it buys is highlights nobody can distinguish. `truncated`, when given,
// says the cap was reached. Navigation is unaffected in kind -- the reader walks
// the matches that are there -- but the count is a floor rather than a total,
// and the caller is expected to say so.
inline constexpr std::size_t kMaxMatches = 100000;

std::vector<TextMatch> findAll(std::string_view text, std::string_view needle,
                               SearchOptions options, bool* truncated = nullptr);

// The same, appending into a vector the caller owns, so a per-keystroke rescan
// reuses the capacity it already has instead of freeing and re-growing it.
// Clears `out` first.
void findAllInto(std::string_view text, std::string_view needle, SearchOptions options,
                 std::vector<TextMatch>* out, bool* truncated = nullptr);

// Index of the first match starting at or after `offset`, or 0 when there is
// none -- which is the wrap. `matches` must be ascending, which is what
// `findAll` returns.
//
// This is where the find bar's "which match am I on" comes from, and it is a
// binary search rather than a walk because the answer is wanted on every
// keystroke over a list that can hold a hundred thousand entries.
std::size_t matchAtOrAfter(const std::vector<TextMatch>& matches, std::size_t offset);

// Stepping: the first match starting strictly after `offset`, and the last one
// starting strictly before it. Both wrap -- past the end is the first, before
// the beginning is the last -- and both return 0 for an empty list, which the
// caller is expected not to index with.
//
// Strictly, because `offset` is where the reader is standing and they are
// usually standing on a match: "at or after" would answer with the match they
// are already on and the key would do nothing. Asking the *list* this, rather
// than adding one to a remembered index, is what makes a click in the middle of
// the note between two presses behave -- the index would step from wherever the
// last press left it, which may be a long way behind the caret.
std::size_t matchAfter(const std::vector<TextMatch>& matches, std::size_t offset);
std::size_t matchBefore(const std::vector<TextMatch>& matches, std::size_t offset);

// There is deliberately no incremental "refine the previous match set" path
// here, though ../microide has one and this engine is otherwise its port.
//
// The refinement it performs is over *lines that held a hit*, rescanned in full.
// The offset-addressed analogue -- treat the previous match starts as the
// candidate set for the longer needle -- is unsound, because the previous scan
// de-overlapped: `aa` in `aaab` matches only at 0, so 1 is not a candidate, and
// yet `aab` matches at 1. Every scan here is therefore a cold one, and the cost
// is kept off the frame by the caller memoising on (revision, needle, options)
// instead. Do not add the candidate-set version; it drops matches, and only for
// needles whose own prefix overlaps itself, which is exactly the case nobody
// tests by hand.

}

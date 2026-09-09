#include "TestSupport.h"

#include "core/util/TextSearch.h"

#include <string>
#include <vector>

using microcore::util::SearchOptions;
using microcore::util::TextMatch;
using microcore::util::findAll;
using microcore::util::findFrom;
using microcore::util::matchAfter;
using microcore::util::matchAtOrAfter;
using microcore::util::matchBefore;
using microcore::util::standsAlone;

namespace {

std::vector<std::size_t> starts(const std::vector<TextMatch>& matches) {
  std::vector<std::size_t> out;
  for(const auto& match : matches) out.push_back(match.start);
  return out;
}

}

MICRONOTES_TEST(text_search_finds_every_occurrence_in_order) {
  const auto matches = findAll("one two one two one", "one", {});
  MICRONOTES_REQUIRE(starts(matches) == (std::vector<std::size_t> {0, 8, 16}));
  MICRONOTES_REQUIRE(matches.front().end == 3);
}

// The default is what a reader typing into a find box expects, and what the
// three separate scans this replaced all happened to do -- except that none of
// them could be told to do anything else.
MICRONOTES_TEST(text_search_ignores_case_unless_asked) {
  MICRONOTES_REQUIRE(findAll("Plan plan PLAN", "plan", {}).size() == 3);
  MICRONOTES_REQUIRE(findAll("Plan plan PLAN", "plan", {.matchCase = true, .wholeWord = false}).size() == 1);
  MICRONOTES_REQUIRE(findAll("Plan plan PLAN", "PLAN", {.matchCase = true, .wholeWord = false}).front().start == 10);
}

// A match consumes its own bytes, so a needle that overlaps itself yields one
// match per non-overlapping run. Two highlights over the same three characters
// is one highlight that looks broken.
MICRONOTES_TEST(text_search_matches_do_not_overlap) {
  MICRONOTES_REQUIRE(starts(findAll("aaaa", "aa", {})) == (std::vector<std::size_t> {0, 2}));
  MICRONOTES_REQUIRE(starts(findAll("aaa", "aa", {})) == (std::vector<std::size_t> {0}));
}

MICRONOTES_TEST(text_search_whole_word_needs_both_edges_clear) {
  const SearchOptions whole {.matchCase = false, .wholeWord = true};
  MICRONOTES_REQUIRE(findAll("plan planner replan a-plan-b", "plan", whole).size() == 2);
  MICRONOTES_REQUIRE(starts(findAll("plan planner replan a-plan-b", "plan", whole)) ==
                     (std::vector<std::size_t> {0, 22}));
  MICRONOTES_REQUIRE(findAll("plan_b", "plan", whole).empty());
}

// A rejected whole-word candidate is not a match, so the bytes it covers stay
// searchable: skipping the needle's length past one would lose a real match
// that starts inside it.
MICRONOTES_TEST(text_search_whole_word_keeps_looking_inside_a_rejected_run) {
  const SearchOptions whole {.matchCase = false, .wholeWord = true};
  MICRONOTES_REQUIRE(starts(findAll("aab aa", "aa", whole)) == (std::vector<std::size_t> {4}));
}

// Every byte of a multi-byte code point counts as a word byte, so "naive" is
// not a whole-word match inside "naïve". Treating a continuation byte as a
// separator is how a whole-word search stops working on prose that is not
// English.
MICRONOTES_TEST(text_search_whole_word_treats_non_ascii_as_word_bytes) {
  const SearchOptions whole {.matchCase = false, .wholeWord = true};
  MICRONOTES_REQUIRE(findAll("na\xc3\xafve", "ve", whole).empty());
  MICRONOTES_REQUIRE(findAll("na\xc3\xafve ve", "ve", whole).size() == 1);
  MICRONOTES_REQUIRE(standsAlone("a plan b", 2, 6));
  MICRONOTES_REQUIRE(!standsAlone("aplan b", 1, 5));
}

MICRONOTES_TEST(text_search_answers_nothing_for_an_impossible_needle) {
  MICRONOTES_REQUIRE(findAll("text", "", {}).empty());
  MICRONOTES_REQUIRE(findAll("ab", "abc", {}).empty());
  MICRONOTES_REQUIRE(findAll("", "a", {}).empty());
  // The miss is the buffer's length, not npos: every caller compares against
  // it, and one sentinel is one branch fewer at each of them.
  MICRONOTES_REQUIRE(findFrom("abc", "z", 0, {}) == 3);
  MICRONOTES_REQUIRE(findFrom("abcabc", "b", 2, {}) == 4);
}

MICRONOTES_TEST(text_search_reports_when_it_stopped_early) {
  bool truncated = true;
  MICRONOTES_REQUIRE(findAll("aaa", "a", {}, &truncated).size() == 3);
  MICRONOTES_REQUIRE(!truncated);

  // One byte per match, so the cap is reached exactly at the ceiling and the
  // remainder is dropped rather than the whole answer being refused.
  const std::string dense(microcore::util::kMaxMatches + 50, 'a');
  const auto capped = findAll(dense, "a", {}, &truncated);
  MICRONOTES_REQUIRE(capped.size() == microcore::util::kMaxMatches);
  MICRONOTES_REQUIRE(truncated);
}

// Stepping asks the list rather than adding one to a remembered index, which is
// what makes a click in the note between two presses of the key behave.
MICRONOTES_TEST(text_search_steps_from_where_the_reader_is) {
  const auto matches = findAll("one two one two one", "one", {});  // 0, 8, 16
  MICRONOTES_REQUIRE(matchAtOrAfter(matches, 0) == 0);
  MICRONOTES_REQUIRE(matchAtOrAfter(matches, 1) == 1);
  MICRONOTES_REQUIRE(matchAtOrAfter(matches, 17) == 0);  // wraps

  // Strictly after and strictly before, so stepping off a match the reader is
  // sitting on lands on its neighbour rather than on itself.
  MICRONOTES_REQUIRE(matchAfter(matches, 0) == 1);
  MICRONOTES_REQUIRE(matchAfter(matches, 8) == 2);
  MICRONOTES_REQUIRE(matchAfter(matches, 16) == 0);
  MICRONOTES_REQUIRE(matchBefore(matches, 16) == 1);
  MICRONOTES_REQUIRE(matchBefore(matches, 8) == 0);
  MICRONOTES_REQUIRE(matchBefore(matches, 0) == 2);

  MICRONOTES_REQUIRE(matchAfter({}, 4) == 0);
  MICRONOTES_REQUIRE(matchBefore({}, 4) == 0);
}

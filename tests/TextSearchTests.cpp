#include "TestSupport.h"

#include "core/perf/PerformanceCounters.h"
#include "core/util/TextSearch.h"

#include <algorithm>
#include <cstdint>
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
using micronotes::tests::counter;

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

// --- the incremental update -------------------------------------------------

namespace {

using microcore::editor::TextEdit;
using microcore::util::findAllInto;
using microcore::util::findAllUpdate;

// One splice, applied to the buffer and to the match list, checked against a
// cold scan of the result. Returns whether the update was taken -- a decline is
// a correct answer too, and the caller counts how often it is not taken.
bool spliceAndCheck(std::string& text, std::vector<TextMatch>& matches, bool& truncated,
                    std::vector<TextMatch>& scratch, std::string_view needle,
                    SearchOptions options, std::size_t start, std::size_t removed,
                    std::string_view inserted, std::uint64_t revision) {
  text.replace(start, removed, inserted);
  const TextEdit edit {revision, revision + 1, start, start + removed, start + inserted.size()};
  const bool updated =
    findAllUpdate(&matches, &scratch, &truncated, text, needle, options, edit);
  if(!updated) findAllInto(text, needle, options, &matches, &truncated);

  std::vector<TextMatch> cold;
  bool coldTruncated = false;
  findAllInto(text, needle, options, &cold, &coldTruncated);
  MICRONOTES_REQUIRE(matches == cold);
  MICRONOTES_REQUIRE(truncated == coldTruncated);
  return updated;
}

std::uint64_t nextRandom(std::uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

}

// A splice is only worth having if it is the same answer as the scan it
// replaces, and the cases it gets wrong are the ones nobody writes by hand: a
// needle whose prefix overlaps itself, an edit landing inside a match, a
// whole-word hit whose separator is the byte that was typed. So the update is
// driven against a cold scan over a random walk, the way the layout's increment
// and the editor's are.
//
// The alphabet is deliberately tiny, which is what makes matches dense and
// makes overlap, adjacency and mid-match edits common rather than rare.
MICRONOTES_TEST(text_search_update_matches_a_cold_scan_under_a_random_edit_walk) {
  const std::vector<std::string> needles {"a", "aa", "aba", "ab", " a ", "b"};
  std::size_t updates = 0;
  std::size_t steps = 0;
  for(const std::string& needle : needles) {
    for(const SearchOptions options :
        {SearchOptions {}, SearchOptions {true, false}, SearchOptions {false, true},
         SearchOptions {true, true}}) {
      std::uint64_t state = 0x9E3779B97F4A7C15ull + needle.size() * 31 +
                            (options.matchCase ? 7 : 0) + (options.wholeWord ? 13 : 0);
      std::string text;
      for(int i = 0; i < 400; ++i) text.push_back("aAb  \n"[nextRandom(state) % 6]);

      std::vector<TextMatch> matches;
      bool truncated = false;
      std::vector<TextMatch> scratch;
      findAllInto(text, needle, options, &matches, &truncated);

      for(std::uint64_t revision = 1; revision <= 300; ++revision) {
        const std::size_t start = text.empty() ? 0 : nextRandom(state) % text.size();
        const std::size_t removed = std::min(nextRandom(state) % 4, text.size() - start);
        std::string inserted;
        for(std::size_t i = nextRandom(state) % 4; i > 0; --i) {
          inserted.push_back("aAb  \n"[nextRandom(state) % 6]);
        }
        ++steps;
        if(spliceAndCheck(text, matches, truncated, scratch, needle, options, start, removed,
                          inserted, revision)) {
          ++updates;
        }
      }
    }
  }
  // The point of the exercise is that the splice is taken, not merely that the
  // fallback is correct: a change that quietly stopped taking it would pass
  // every assertion above.
  MICRONOTES_REQUIRE(steps == 7200);
  MICRONOTES_REQUIRE(updates > steps * 9 / 10);
}

// The window the update reads is bounded by the edit, not by the buffer. That is
// the whole of TD-47, and the counters are how it is asserted: a keystroke in a
// large note must not put the note's size through the scanner again.
MICRONOTES_TEST(text_search_update_reads_the_edit_rather_than_the_note) {
  using microcore::perf::CounterId;
  std::string text;
  while(text.size() < 200000) text += "the quick brown fox jumps over the lazy dog\n";

  std::vector<TextMatch> matches;
  std::vector<TextMatch> scratch;
  bool truncated = false;
  findAllInto(text, "fox", {}, &matches, &truncated);
  MICRONOTES_REQUIRE(matches.size() > 4000);

  const auto scanBytesBefore = counter(CounterId::TextSearchScanBytes);
  const auto updatesBefore = counter(CounterId::TextSearchUpdates);
  const auto updateBytesBefore = counter(CounterId::TextSearchUpdateBytes);

  const std::size_t at = text.size() / 2;
  text.insert(at, "z");
  const TextEdit edit {1, 2, at, at, at + 1};
  MICRONOTES_REQUIRE(findAllUpdate(&matches, &scratch, &truncated, text, "fox", {}, edit));

  MICRONOTES_REQUIRE(counter(CounterId::TextSearchUpdates) - updatesBefore == 1);
  // Nothing went through the cold scanner at all.
  MICRONOTES_REQUIRE(counter(CounterId::TextSearchScanBytes) == scanBytesBefore);
  // And the window is a handful of bytes, not 200 KB. It is not zero: the walk
  // has to reach a boundary the old list also had, which is the next match.
  const std::uint64_t windowBytes = counter(CounterId::TextSearchUpdateBytes) - updateBytesBefore;
  MICRONOTES_REQUIRE(windowBytes < 200);

  std::vector<TextMatch> cold;
  findAllInto(text, "fox", {}, &cold, nullptr);
  MICRONOTES_REQUIRE(matches == cold);
}

// A list that hit the cap cannot be spliced: an insertion before the cut-off
// moves what fell off the end, and the update has no record of it. It declines,
// which is what leaves `truncated` honest.
MICRONOTES_TEST(text_search_update_declines_a_truncated_list) {
  std::string text(microcore::util::kMaxMatches + 10, 'a');
  std::vector<TextMatch> matches;
  std::vector<TextMatch> scratch;
  bool truncated = false;
  findAllInto(text, "a", {}, &matches, &truncated);
  MICRONOTES_REQUIRE(truncated);
  MICRONOTES_REQUIRE(matches.size() == microcore::util::kMaxMatches);

  text.insert(0, "a");
  const TextEdit edit {1, 2, 0, 0, 1};
  MICRONOTES_REQUIRE(!findAllUpdate(&matches, &scratch, &truncated, text, "a", {}, edit));
  MICRONOTES_REQUIRE(truncated);
}

// An edit with no stamps -- a test, the harness, the first frame after a note
// opens -- is not an edit this can place, and it says so rather than assuming
// the buffer it is holding is the one before it.
MICRONOTES_TEST(text_search_update_declines_an_edit_it_cannot_place) {
  std::string text = "one two one";
  std::vector<TextMatch> matches = findAll(text, "one", {});
  std::vector<TextMatch> scratch;
  bool truncated = false;
  MICRONOTES_REQUIRE(!findAllUpdate(&matches, &scratch, &truncated, text, "one", {}, TextEdit {}));
  // An edit reaching past the end of the buffer it was given is not one either.
  MICRONOTES_REQUIRE(
    !findAllUpdate(&matches, &scratch, &truncated, text, "one", {}, TextEdit {1, 2, 0, 0, 900}));
  MICRONOTES_REQUIRE(matches.size() == 2);
}

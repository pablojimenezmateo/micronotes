#include "TestSupport.h"

#include "doc/InlineScan.h"
#include "ui/WikiLink.h"

#include <limits>
#include <string_view>
#include <string>
#include <vector>

using micronotes::library::NoteListItem;
using micronotes::ui::resolveWikiLink;
using micronotes::ui::retargetWikiLinks;
using micronotes::ui::splitWikiTarget;
using micronotes::ui::wikiReferences;

namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

NoteListItem note(std::string title, std::string path) {
  NoteListItem item;
  item.id = path;
  item.title = std::move(title);
  item.path = std::filesystem::path(std::move(path));
  return item;
}

}

MICRONOTES_TEST(wiki_target_splits_off_the_heading) {
  MICRONOTES_REQUIRE(splitWikiTarget("Note").note == "Note");
  MICRONOTES_REQUIRE(splitWikiTarget("Note").heading.empty());
  MICRONOTES_REQUIRE(splitWikiTarget("Note#Part").note == "Note");
  MICRONOTES_REQUIRE(splitWikiTarget("Note#Part").heading == "Part");
  // Written with spaces round the parts, as people do.
  MICRONOTES_REQUIRE(splitWikiTarget(" Note # Part ").note == "Note");
  MICRONOTES_REQUIRE(splitWikiTarget(" Note # Part ").heading == "Part");
}

MICRONOTES_TEST(wiki_link_resolves_by_title_first) {
  const std::vector<NoteListItem> notes {
    note("Alpha", "Alpha.md"),
    note("Beta", "work/Beta.md"),
  };
  MICRONOTES_REQUIRE(resolveWikiLink("Alpha", notes) == 0);
  MICRONOTES_REQUIRE(resolveWikiLink("Beta", notes) == 1);
  MICRONOTES_REQUIRE(resolveWikiLink("Gamma", notes) == kNone);
}

// An exact title beats a case-insensitive one anywhere in the library, so the
// weaker rule cannot win just by appearing earlier in the list.
MICRONOTES_TEST(wiki_link_prefers_the_stronger_rule_over_the_earlier_note) {
  const std::vector<NoteListItem> notes {
    note("alpha", "lower.md"),
    note("Alpha", "exact.md"),
  };
  MICRONOTES_REQUIRE(resolveWikiLink("Alpha", notes) == 1);
}

MICRONOTES_TEST(wiki_link_falls_back_to_the_file_name) {
  const std::vector<NoteListItem> notes {
    note("A title nobody types", "shorthand.md"),
  };
  MICRONOTES_REQUIRE(resolveWikiLink("shorthand", notes) == 0);
  MICRONOTES_REQUIRE(resolveWikiLink("SHORTHAND", notes) == 0);
}

MICRONOTES_TEST(wiki_link_resolves_a_path_with_the_extension_optional) {
  const std::vector<NoteListItem> notes {
    note("Roadmap", "projects/Roadmap.md"),
    note("Roadmap", "archive/2024/Roadmap.md"),
  };
  MICRONOTES_REQUIRE(resolveWikiLink("projects/Roadmap", notes) == 0);
  MICRONOTES_REQUIRE(resolveWikiLink("projects/Roadmap.md", notes) == 0);
  MICRONOTES_REQUIRE(resolveWikiLink("archive/2024/Roadmap", notes) == 1);
}

// Two notes with the same name: the one nearer the root is what a bare title
// almost always meant.
MICRONOTES_TEST(wiki_link_breaks_a_tie_by_going_nearer_the_root) {
  const std::vector<NoteListItem> notes {
    note("Notes", "a/b/c/Notes.md"),
    note("Notes", "Notes.md"),
  };
  MICRONOTES_REQUIRE(resolveWikiLink("Notes", notes) == 1);
}

// The rule that keeps a link readable in any other editor.
MICRONOTES_TEST(wiki_link_never_resolves_by_the_invisible_id) {
  std::vector<NoteListItem> notes {note("Visible title", "file.md")};
  notes[0].id = "01HXYZ-generated-id";
  MICRONOTES_REQUIRE(resolveWikiLink("01HXYZ-generated-id", notes) == kNone);
  MICRONOTES_REQUIRE(resolveWikiLink("Visible title", notes) == 0);
}

MICRONOTES_TEST(wiki_link_ignores_a_heading_when_finding_the_note) {
  const std::vector<NoteListItem> notes {note("Alpha", "Alpha.md")};
  MICRONOTES_REQUIRE(resolveWikiLink("Alpha#Some section", notes) == 0);
}

MICRONOTES_TEST(wiki_references_report_every_link_with_its_line) {
  const auto refs = wikiReferences("# Title\n\nSee [[Alpha]] and [[Beta|b]] for more.\n\nNothing here.\n");
  MICRONOTES_REQUIRE(refs.size() == 2);
  MICRONOTES_REQUIRE(refs[0].target == "Alpha");
  MICRONOTES_REQUIRE(refs[1].target == "Beta");
  // The line is what turns a list of titles into a reason to click.
  MICRONOTES_REQUIRE(refs[0].line == "See [[Alpha]] and [[Beta|b]] for more.");
  MICRONOTES_REQUIRE(refs[1].line == refs[0].line);
}

MICRONOTES_TEST(wiki_references_skip_a_link_inside_a_code_span) {
  MICRONOTES_REQUIRE(wikiReferences("`[[Alpha]]`\n").empty());
}

// A rename touches the target and nothing else: the alias and the fragment are
// the writer's words about this particular link.
MICRONOTES_TEST(wiki_retarget_rewrites_only_the_note_part) {
  const std::string source = "[[Old]] and [[Old|call it this]] and [[Old#Part]] and [[Older]]\n";
  const auto rewritten = retargetWikiLinks(source, "Old", "New");
  MICRONOTES_REQUIRE(rewritten == "[[New]] and [[New|call it this]] and [[New#Part]] and [[Older]]\n");
}

MICRONOTES_TEST(wiki_retarget_leaves_a_note_with_no_matching_link_untouched) {
  const std::string source = "Nothing to see, and [[Something Else]].\n";
  MICRONOTES_REQUIRE(retargetWikiLinks(source, "Old", "New") == source);
}

MICRONOTES_TEST(wiki_retarget_handles_a_link_at_the_very_start_and_end) {
  MICRONOTES_REQUIRE(retargetWikiLinks("[[A]]", "A", "B") == "[[B]]");
  MICRONOTES_REQUIRE(retargetWikiLinks("x [[A]]", "A", "B") == "x [[B]]");
}

// `findWikiLink` applies `doc::InlineScan`'s wikilink rule to a bare string,
// which is what the reading pane needs: md4c hands `[[Some Note]]` back as
// literal text, split at every `[`, so the pane has nothing but a string.
MICRONOTES_TEST(find_wiki_link_reads_a_bare_target) {
  const auto span = micronotes::ui::findWikiLink("see [[Some Note]] there");
  MICRONOTES_REQUIRE(span.has_value());
  MICRONOTES_REQUIRE(span->start == 4);
  MICRONOTES_REQUIRE(span->end == 17);
  MICRONOTES_REQUIRE(span->target == "Some Note");
  // With no alias, the target is also what gets drawn.
  MICRONOTES_REQUIRE(span->label == "Some Note");
}

MICRONOTES_TEST(find_wiki_link_splits_an_alias_from_its_target) {
  const auto span = micronotes::ui::findWikiLink("[[Deep/Note|what to call it]]");
  MICRONOTES_REQUIRE(span.has_value());
  MICRONOTES_REQUIRE(span->target == "Deep/Note");
  MICRONOTES_REQUIRE(span->label == "what to call it");
}

MICRONOTES_TEST(find_wiki_link_walks_from_an_offset) {
  const std::string_view text = "[[one]] and [[two]]";
  const auto first = micronotes::ui::findWikiLink(text);
  MICRONOTES_REQUIRE(first.has_value() && first->target == "one");
  const auto second = micronotes::ui::findWikiLink(text, first->end);
  MICRONOTES_REQUIRE(second.has_value() && second->target == "two");
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink(text, second->end).has_value());
}

MICRONOTES_TEST(find_wiki_link_leaves_brackets_that_are_not_links_alone) {
  // Unterminated, empty, and an alias with no target: all of them are the
  // literal text somebody typed, and drawing them as links would invent a
  // destination.
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink("[[never closed").has_value());
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink("[[]]").has_value());
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink("[[|alias]]").has_value());
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink("[[target|]]").has_value());
  // A single-bracket Markdown link is not one either.
  MICRONOTES_REQUIRE(!micronotes::ui::findWikiLink("[a](b)").has_value());
}

MICRONOTES_TEST(find_wiki_link_closes_on_the_first_double_bracket) {
  // The same rule `doc::InlineScan` documents: a `[` inside is somebody
  // typing, because there is no such thing as a nested wikilink.
  const auto span = micronotes::ui::findWikiLink("[[a [[b]] c]]");
  MICRONOTES_REQUIRE(span.has_value());
  MICRONOTES_REQUIRE(span->target == "a [[b");
}

// The rule above has to agree with the scanner the live surface uses, or the
// two panes disagree about what is a link in the same note.
MICRONOTES_TEST(find_wiki_link_agrees_with_the_inline_scanner) {
  const std::string_view source = "a [[One]] b [[Two|second]] c [[unclosed";
  std::vector<std::string> fromScan;
  for(const auto& span : micronotes::doc::scanInlines(source, 0)) {
    if(span.kind == micronotes::doc::SpanKind::WikiLink) fromScan.push_back(span.target);
  }
  std::vector<std::string> fromFind;
  for(std::size_t at = 0;;) {
    const auto span = micronotes::ui::findWikiLink(source, at);
    if(!span) break;
    fromFind.push_back(span->target);
    at = span->end;
  }
  MICRONOTES_REQUIRE(fromScan == fromFind);
}

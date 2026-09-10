#include "TestSupport.h"
#include "TempDir.h"

#include "library/LibraryIndex.h"
#include "library/Library.h"
#include "library/Metadata.h"
#include "library/NoteCatalog.h"

#include <filesystem>
#include <fstream>
#include <string>

// Turning a query into results: which notes come back, and the lines shown
// under each one.
//
// This file was a catch-all for everything the `library` layer's index does --
// the query, keeping the index in step with the files, the links table and the
// organization service -- in 902 lines, four subjects sharing one set of
// includes. `library_index_*` is not a subject; it is a prefix.

MICRONOTES_TEST(library_index_rebuilds_sqlite_cache_from_files) {
  const micronotes::tests::TempDir rootDir("micronotes-index-test");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "note-search";
  metadata.title = "SQLite Fast Path";
  library.createNote(metadata, "needle body\nmiddle\nsecond needle line");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].id == "note-search");
  MICRONOTES_REQUIRE(results[0].snippets.size() == 2);
  MICRONOTES_REQUIRE(results[0].snippets[0].matchLine == "needle body");
  // Where in each line the query landed, so the sidebar can mark the match
  // rather than the whole line -- and trim a long line around it rather than
  // ellipsizing the match itself away.
  MICRONOTES_REQUIRE(results[0].snippets[0].matchStart == 0);
  MICRONOTES_REQUIRE(results[0].snippets[0].matchLength == 6);
  MICRONOTES_REQUIRE(results[0].snippets[1].matchLine == "second needle line");
  MICRONOTES_REQUIRE(results[0].snippets[1].matchStart == 7);
  MICRONOTES_REQUIRE(results[0].snippets[1].matchLength == 6);
  const auto partial = index.search("eedle bo");
  MICRONOTES_REQUIRE(partial.size() == 1);
  MICRONOTES_REQUIRE(partial[0].id == "note-search");
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("SQLite", micronotes::library::SearchScope::Content).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Title).empty());
  MICRONOTES_REQUIRE(index.search("needle", micronotes::library::SearchScope::Content).size() == 1);
  MICRONOTES_REQUIRE(std::filesystem::exists(root / ".micronotes" / "index.sqlite"));
}

// The match range is a byte offset into the line as written, not into a
// lowercased copy of it, so a query in the other case still points at the
// right bytes.
MICRONOTES_TEST(library_index_locates_a_match_regardless_of_case) {
  const micronotes::tests::TempDir rootDir("micronotes-index-match-case");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "cased";
  metadata.title = "Cased";
  library.createNote(metadata, "The Needle is capitalised here\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].snippets.size() == 1);
  const auto& snippet = results[0].snippets.front();
  MICRONOTES_REQUIRE(snippet.matchStart == 4);
  MICRONOTES_REQUIRE(snippet.matchLine.substr(snippet.matchStart, snippet.matchLength) == "Needle");
}

// A query matching hundreds of lines of one note used to build a snippet per
// line, three strings each, and throw all but three away -- per note, on every
// keystroke of the query.
MICRONOTES_TEST(library_index_keeps_only_the_snippets_anything_will_draw) {
  const micronotes::tests::TempDir rootDir("micronotes-index-snippet-cap");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "many";
  metadata.title = "Many";
  std::string body;
  for(int i = 0; i < 500; ++i) body += "a needle on line " + std::to_string(i) + "\n";
  library.createNote(metadata, body);

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  MICRONOTES_REQUIRE(results[0].snippets.size() == 3);
  // Still the first three lines, in order, rather than an arbitrary three.
  MICRONOTES_REQUIRE(results[0].snippets[0].matchLine == "a needle on line 0");
  MICRONOTES_REQUIRE(results[0].snippets[2].matchLine == "a needle on line 2");
}

// A snippet is a three-line window, and which three lines it is is the part a
// streaming walk over the body can get wrong: the line above has to be the one
// the previous turn of the loop saw, and the line below is not known until the
// turn after. The first and last lines of a note have no neighbour on one side,
// which is where an off-by-one shows up as somebody else's text.
MICRONOTES_TEST(library_index_snippets_carry_the_lines_around_the_match) {
  const micronotes::tests::TempDir rootDir("micronotes-index-snippet-window");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "window";
  metadata.title = "Window";
  library.createNote(metadata, "needle at the top\nsecond\nthird\nneedle in the middle\nfifth\nneedle at the end");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());
  const auto results = index.search("needle");
  MICRONOTES_REQUIRE(results.size() == 1);
  const auto& snippets = results[0].snippets;
  MICRONOTES_REQUIRE(snippets.size() == 3);
  // Nothing above the first line of the note.
  MICRONOTES_REQUIRE(snippets[0].beforeLine.empty());
  MICRONOTES_REQUIRE(snippets[0].matchLine == "needle at the top");
  MICRONOTES_REQUIRE(snippets[0].afterLine == "second");
  MICRONOTES_REQUIRE(snippets[1].beforeLine == "third");
  MICRONOTES_REQUIRE(snippets[1].matchLine == "needle in the middle");
  MICRONOTES_REQUIRE(snippets[1].afterLine == "fifth");
  // Nothing below the last, and no trailing newline to invent one.
  MICRONOTES_REQUIRE(snippets[2].beforeLine == "fifth");
  MICRONOTES_REQUIRE(snippets[2].matchLine == "needle at the end");
  MICRONOTES_REQUIRE(snippets[2].afterLine.empty());
  // And `firstMatch` is the first of them rather than a copy of it -- the
  // copy is what this used to hold, six strings per result that nothing wrote
  // but the fill and nothing read but through a branch that could not run.
  MICRONOTES_REQUIRE(results[0].firstMatch() == &snippets.front());
}

// `%` and `_` are SQL LIKE's own wildcards. A query carrying one used to match
// notes that do not contain the query at all, and each of those rows drew a
// title with nothing under it -- the snippet under a result is found by a
// literal search of the body, and there was nothing literal there to find.
MICRONOTES_TEST(library_index_treats_sql_wildcards_as_ordinary_characters) {
  const micronotes::tests::TempDir rootDir("micronotes-index-wildcards");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "w";
  metadata.title = "Wildcards";
  library.createNote(metadata, "growth was 50 percent\nand ab_cd here\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());

  // "a%t" would have matched every note with an `a` somewhere before a `t`.
  MICRONOTES_REQUIRE(index.search("a%t").empty());
  MICRONOTES_REQUIRE(index.search("50%").empty());
  // A literal underscore still finds the text that literally has one, and an
  // underscore must not stand in for the character beside it.
  MICRONOTES_REQUIRE(index.search("ab_cd").size() == 1);
  MICRONOTES_REQUIRE(index.search("ab_d").empty());
  // A trailing backslash is escaped too, or it would escape the pattern's own
  // closing wildcard and match nothing at all by accident.
  MICRONOTES_REQUIRE(index.search("percent\\").empty());
  MICRONOTES_REQUIRE(index.search("percent").size() == 1);
}

// The substring fallback, which is the branch nothing else covers.
//
// FTS matches whole terms, so a query that is the *middle* of a word never
// reaches a note through it and falls through to the `LIKE '%q%'` scan. That
// scan has to fold case, and where the folding happens is the difference
// between comparing the query against every note's body and building a lowered
// copy of every note's body to compare it against. Both answer the same, so a
// test that only asked through FTS could not tell them apart -- and the whole
// library is scanned on every keystroke that has no match yet, which is every
// keystroke of a query being typed.
MICRONOTES_TEST(library_index_finds_a_mid_word_substring_in_any_case) {
  const micronotes::tests::TempDir rootDir("micronotes-index-substring-case");
  const auto& root = rootDir.path();
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "sub";
  metadata.title = "Capitalised Heading";
  library.createNote(metadata, "The word CAPITALISED appears here\n");

  micronotes::library::LibraryIndex index;
  MICRONOTES_REQUIRE(index.open(root));
  MICRONOTES_REQUIRE(index.rebuild());

  // Mid-word, so FTS cannot answer: the fallback does, in either case and
  // against either column.
  MICRONOTES_REQUIRE(index.search("pital").size() == 1);
  MICRONOTES_REQUIRE(index.search("PITAL").size() == 1);
  MICRONOTES_REQUIRE(index.search("PiTaL").size() == 1);
  MICRONOTES_REQUIRE(index.search("pital", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("PITAL", micronotes::library::SearchScope::Title).size() == 1);
  MICRONOTES_REQUIRE(index.search("PITAL", micronotes::library::SearchScope::Content).size() == 1);
  // And the snippet under it is found in the body as written, whatever case the
  // query arrived in.
  const auto results = index.search("PITAL", micronotes::library::SearchScope::Content);
  MICRONOTES_REQUIRE(results[0].snippets.size() == 1);
  const auto& snippet = results[0].snippets.front();
  MICRONOTES_REQUIRE(snippet.matchLine.substr(snippet.matchStart, snippet.matchLength) == "PITAL");
  MICRONOTES_REQUIRE(index.search("nothinglikethis").empty());
}

// A companion file is found by its name and by nothing else: its contents are
// not micronotes' to read, so a search scoped to content finds none.
MICRONOTES_TEST(library_finds_companion_files_by_name_only) {
  const micronotes::tests::TempDir rootDir("micronotes-companion-search-test");
  const auto& root = rootDir.path();
  const auto touch = [&](const std::filesystem::path& relative, const char* text) {
    std::filesystem::create_directories((root / relative).parent_path());
    std::ofstream out(root / relative);
    out << text;
  };
  micronotes::library::Library library(root);
  micronotes::library::NoteMetadata metadata;
  metadata.id = "plan-note";
  metadata.title = "Plan";
  library.createNote(metadata, "the report body");
  touch("files/Report.PDF", "pdf bytes");
  touch("files/report-notes/other.txt", "report inside, name outside");
  touch("work/files/plan.pdf", "pdf bytes");

  micronotes::library::NoteCatalog catalog;
  MICRONOTES_REQUIRE(catalog.open(root));
  MICRONOTES_REQUIRE(catalog.companions().size() == 6);

  // Case-insensitive on the name, and a directory whose name matches is not a
  // hit -- there is nothing to open.
  const auto report = catalog.searchCompanions("report", micronotes::library::SearchScope::All);
  MICRONOTES_REQUIRE(report.size() == 1);
  MICRONOTES_REQUIRE(report[0].path == std::filesystem::path("files/Report.PDF"));
  const auto pdf = catalog.searchCompanions("PDF", micronotes::library::SearchScope::Title);
  MICRONOTES_REQUIRE(pdf.size() == 2);
  // The word is inside `other.txt` and in the body of the note, and the
  // companion search sees neither.
  MICRONOTES_REQUIRE(catalog.searchCompanions("inside", micronotes::library::SearchScope::All).empty());
  MICRONOTES_REQUIRE(catalog.searchCompanions("pdf", micronotes::library::SearchScope::Content).empty());
  // The note search is untouched by the files beside it.
  MICRONOTES_REQUIRE(catalog.search("report", micronotes::library::SearchScope::All).size() == 1);
}

#include "library/LibraryIndex.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/persistence/SqliteDb.h"
#include "core/util/StringUtil.h"
#include "library/SearchScope.h"

#include <sqlite3.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Turning a query into results, and a result into the three lines of the note
// worth showing under it.
//
// Split from `LibraryIndex.cpp`, which held the schema, the row writer, the
// re-indexing walk and this in 904 lines. Almost none of that is about
// searching, and the part that is has the file's only interesting non-SQL
// logic in it: the snippet walk below is a hand-written scan over a note body
// with a documented allocation budget, sitting in a file otherwise made of
// prepared statements.

namespace micronotes::library {

using persistence::SqliteDb;
using persistence::Statement;

namespace {

static std::string lowerCopy(std::string value) {
  util::toLowerAsciiInPlace(value);
  return value;
}

// As many snippets as anything downstream will draw, and not one more. The
// sidebar shows three lines per result; a query matching a thousand lines of
// one note used to build a thousand snippets -- three strings each -- and throw
// all but three away, once per note, on every query.
static constexpr std::size_t kMaxSnippets = 3;

// A `LIKE` pattern matching `lowerQuery` anywhere, with LIKE's own wildcards
// taken literally. Takes the query already lowered, because the one caller has
// it lowered for the snippet search too.
//
// `%` and `_` mean "anything" to LIKE, so a query carrying one used to match
// notes that do not contain the query at all -- "a%t" matched every note with
// an `a` somewhere before a `t` -- and each of those rows then drew a title
// with nothing under it, because the snippet beneath a result is found by a
// literal search of the body and there was nothing literal there to find. The
// escape character has to be escaped too, or a query ending in a backslash
// would escape the pattern's own closing wildcard.
static std::string likePattern(std::string_view lowerQuery) {
  std::string out;
  out.reserve(lowerQuery.size() + 2);
  out.push_back('%');
  for(const char c : lowerQuery) {
    if(c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('%');
  return out;
}

// Case-insensitive `find`, over views. `query` is already lowered.
static std::size_t findLowered(std::string_view line, std::string_view lowerQuery) {
  if(lowerQuery.empty() || line.size() < lowerQuery.size()) return std::string_view::npos;
  const std::size_t last = line.size() - lowerQuery.size();
  for(std::size_t at = 0; at <= last; ++at) {
    std::size_t i = 0;
    while(i < lowerQuery.size() && util::toLowerAscii(line[at + i]) == lowerQuery[i]) ++i;
    if(i == lowerQuery.size()) return at;
  }
  return std::string_view::npos;
}

// The line `body` holds at `[from, ...)`, and where the one after it starts.
// The same partition `std::getline` produced: the newline belongs to neither
// line, and a buffer ending in one does not yield an empty line after it.
static std::string_view lineAt(std::string_view body, std::size_t from, std::size_t* next) {
  const auto end = body.find('\n', from);
  if(end == std::string_view::npos) {
    *next = body.size();
    return body.substr(from);
  }
  *next = end + 1;
  return body.substr(from, end - from);
}

// Walks `body` a line at a time as views, and materialises a string only for
// the at most three snippets it keeps.
//
// This used to copy the whole body into an `istringstream`, then allocate a
// `std::string` per line of it into a vector, then allocate a lowered copy of
// each of those to search it -- three allocations per line of every note in the
// result set, to show three lines of one. On a query returning 200 notes that
// was the whole cost of the query. Nothing is allocated here per line, and the
// walk stops as soon as the third snippet has the line under it.
// `lowerQuery` is lowered once by the caller: it is the same string for every
// row of a result set, and lowering it here allocated twice per row to rebuild
// a constant.
static void fillSnippet(SearchResult& result, std::string_view body, std::string_view lowerQuery) {
  if(lowerQuery.empty()) return;

  constexpr std::size_t kNone = static_cast<std::size_t>(-1);
  std::string_view previous;      // the line above the one being tested
  std::size_t awaiting = kNone;   // a snippet still missing the line below it
  std::size_t at = 0;
  while(at < body.size()) {
    std::size_t next = 0;
    const std::string_view line = lineAt(body, at, &next);
    at = next;
    if(awaiting != kNone) {
      result.snippets[awaiting].afterLine = std::string(line);
      awaiting = kNone;
    }
    if(result.snippets.size() < kMaxSnippets) {
      const auto found = findLowered(line, lowerQuery);
      if(found != std::string_view::npos) {
        SearchResult::Snippet snippet;
        snippet.beforeLine = std::string(previous);
        snippet.matchLine = std::string(line);
        // Byte offsets into the line as written. Lowercasing is one-for-one
        // over the bytes this comparison can match -- ASCII letters -- so the
        // offset found against the lowered query addresses the same bytes in
        // the original.
        snippet.matchStart = found;
        snippet.matchLength = lowerQuery.size();
        result.snippets.push_back(std::move(snippet));
        awaiting = result.snippets.size() - 1;
      }
    }
    previous = line;
    if(awaiting == kNone && result.snippets.size() >= kMaxSnippets) break;
  }
  if(!result.snippets.empty()) {
    result.beforeLine = result.snippets.front().beforeLine;
    result.matchLine = result.snippets.front().matchLine;
    result.afterLine = result.snippets.front().afterLine;
    result.matchStart = result.snippets.front().matchStart;
    result.matchLength = result.snippets.front().matchLength;
  }
}

static void collectRows(sqlite3_stmt* stmt, std::vector<SearchResult>& out,
                        std::string_view lowerQuery) {
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    // Through `columnText`, which is the same read the refresh uses. Building a
    // `std::string` straight from `sqlite3_column_text` is a construction from
    // `nullptr` on a NULL column: unreachable today -- every column read here is
    // declared NOT NULL or comes through an inner join -- but the two spellings
    // sat four functions apart in this file and only one of them was safe.
    out.push_back({persistence::columnText(stmt, 0), {}, persistence::columnText(stmt, 2)});
    out.back().path = persistence::columnText(stmt, 1);
    // The body is *read*, never kept, so it is borrowed from sqlite rather than
    // copied out. It was a whole-note allocation per result row -- up to 200 of
    // them, of which at most three lines of one are ever shown -- and on a
    // library of large notes that copy was the search.
    if(const auto body = persistence::columnView(stmt, 3); !body.empty()) {
      fillSnippet(out.back(), body, lowerQuery);
    }
  }
}

}

std::vector<SearchResult> LibraryIndex::search(std::string_view query, SearchScope scope) const {
  perf::ScopeTimer timer("library_index.search");
  perf::addCounter(perf::CounterId::LibrarySearchCalls);
  std::vector<SearchResult> out;
  if(query.empty()) return out;
  if(db_.isOpen()) {
    SqliteDb& db = db_;
    {
      const char* ftsSql = scope == SearchScope::Title
        ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts.title MATCH ? ORDER BY rank LIMIT 200;"
        : scope == SearchScope::Content
          ? "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts.body MATCH ? ORDER BY rank LIMIT 200;"
          : "SELECT notes.id, notes.path, notes.title, notes.body FROM notes_fts JOIN notes ON notes.rowid = notes_fts.rowid WHERE notes_fts MATCH ? ORDER BY rank LIMIT 200;";
      // Lowered once for the whole query rather than once per row.
      const std::string lowerQuery = lowerCopy(std::string(query));
      if(Statement stmt = db.prepare(ftsSql); stmt) {
        const std::string q(query);
        persistence::bindText(stmt, 1, q);
        collectRows(stmt, out, lowerQuery);
      }
      if(out.empty()) {
        // No `lower()` around the columns, and that is the difference between
        // scanning the library and building a lowered copy of it to scan.
        //
        // SQLite's `LIKE` folds ASCII case itself unless `case_sensitive_like`
        // is turned on, which nothing here does -- and `lower()` folds ASCII
        // and nothing else, so the two agreed exactly. What they did not agree
        // on is cost: `lower(body)` materialises a second copy of every note's
        // body, for every row of the table, to answer a question `LIKE` would
        // have answered against the bytes already in the page cache.
        //
        // This is the branch a query being typed spends its whole life in --
        // FTS matches whole terms, so every keystroke before the query becomes
        // a word falls through to here and scans the entire library.
        const char* likeSql = scope == SearchScope::Title
          ? "SELECT id,path,title,body FROM notes WHERE title LIKE ? ESCAPE '\\' ORDER BY title LIMIT 200;"
          : scope == SearchScope::Content
            ? "SELECT id,path,title,body FROM notes WHERE body LIKE ? ESCAPE '\\' ORDER BY title LIMIT 200;"
            : "SELECT id,path,title,body FROM notes WHERE title LIKE ?1 ESCAPE '\\' OR body LIKE ?2 ESCAPE '\\' OR path LIKE ?3 ESCAPE '\\' ORDER BY title LIMIT 200;";
        if(Statement stmt = db.prepare(likeSql); stmt) {
          const std::string q = likePattern(lowerQuery);
          persistence::bindText(stmt, 1, q);
          if(scope == SearchScope::All) {
            persistence::bindText(stmt, 2, q);
            persistence::bindText(stmt, 3, q);
          }
          collectRows(stmt, out, lowerQuery);
        }
      }
      for(auto& result : out) result.path = root_ / result.path;
      return out;
    }
  }
  // No connection, so no rows. There used to be an in-memory projection of the
  // table here to fall back on -- a thousand `SearchResult`s rebuilt on every
  // refresh that found work, which is every save -- and it was unreachable:
  // nothing filled it unless the database was open, and while the database is
  // open the branch above always returns.
  return out;
}

std::vector<Backlink> LibraryIndex::backlinks(std::string_view title, std::string_view stem) const {
  std::vector<Backlink> found;
  if(!db_.isOpen() || (title.empty() && stem.empty())) return found;
  // NOCASE so a link written in a different case still counts, which is the
  // same latitude resolveWikiLink gives it.
  Statement stmt = db_.prepare(
    "SELECT notes.id, notes.path, notes.title, links.line FROM links "
    "JOIN notes ON notes.id = links.src_id "
    "WHERE links.target = ?1 COLLATE NOCASE OR links.target = ?2 COLLATE NOCASE "
    "ORDER BY notes.title;");
  if(!stmt) return found;
  persistence::bindText(stmt, 1, std::string(title));
  persistence::bindText(stmt, 2, std::string(stem));
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    found.push_back({persistence::columnText(stmt, 0), std::filesystem::path(persistence::columnText(stmt, 1)),
                     persistence::columnText(stmt, 2), persistence::columnText(stmt, 3)});
  }
  return found;
}

}

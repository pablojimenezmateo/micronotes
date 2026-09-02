#pragma once

#include "CoreAliases.h"

#include "core/persistence/SqliteDb.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micronotes::library {

enum class SearchScope {
  All,
  Title,
  Content
};

struct SearchResult {
  struct Snippet {
    std::string beforeLine {};
    std::string matchLine {};
    std::string afterLine {};
    // Where the query landed inside `matchLine`, in bytes. The sidebar marks
    // the span rather than the line, and trims a line too long for its column
    // around the match instead of from its end -- without a range it could do
    // neither, and listed a note as matching next to a line with no visible
    // reason why. Length zero means the line matched but the position is not
    // known, which is what a match on the note's title rather than its text
    // looks like.
    std::size_t matchStart = 0;
    std::size_t matchLength = 0;
  };

  std::string id;
  std::filesystem::path path;
  std::string title;
  std::string beforeLine {};
  std::string matchLine {};
  std::string afterLine {};
  std::size_t matchStart = 0;
  std::size_t matchLength = 0;
  // Capped to what anything downstream will draw: the sidebar shows three
  // lines per result, and a query matching a thousand lines of one note used to
  // build a thousand snippets and throw all but three of them away.
  std::vector<Snippet> snippets {};
};

// One note pointing at another, with the line it did so on. The line is what
// separates a useful backlinks panel from a list of titles.
struct Backlink {
  std::string id;
  std::filesystem::path path;
  std::string title;
  std::string line;
};

class LibraryIndex {
public:
  bool open(const std::filesystem::path& libraryRoot);
  bool migrate();
  bool rebuild();
  bool refreshChangedFiles();
  std::vector<SearchResult> search(std::string_view query, SearchScope scope = SearchScope::All) const;

  // Every note whose text carries a `[[target]]` naming this one.
  //
  // The stored target is the text as written, and matching happens here rather
  // than at index time. That is deliberate: an index of resolved ids would have
  // to be found and rewritten on every rename, whereas matching at query time
  // means a rename changes what resolves without touching a single row.
  // `title` and `stem` are the two spellings a link is allowed to use.
  std::vector<Backlink> backlinks(std::string_view title, std::string_view stem) const;
  std::size_t size() const;

private:
  std::filesystem::path root_;
  std::filesystem::path dbPath_;
  std::vector<SearchResult> rows_;
  // One connection for the index's lifetime. Every method used to open its own,
  // which recompiled its SQL each call and left sqlite.connection_opens reading
  // zero -- the counter looked like "no connections" rather than "not measured".
  //
  // mutable because search() is const but still needs the connection and its
  // statement cache; the database is an implementation detail, not part of the
  // index's logical value.
  mutable persistence::SqliteDb db_;
};

}

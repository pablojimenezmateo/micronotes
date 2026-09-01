#pragma once

#include "CoreAliases.h"

#include "core/persistence/SqliteDb.h"

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
  };

  std::string id;
  std::filesystem::path path;
  std::string title;
  std::string beforeLine {};
  std::string matchLine {};
  std::string afterLine {};
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

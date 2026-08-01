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

class LibraryIndex {
public:
  bool open(const std::filesystem::path& libraryRoot);
  bool migrate();
  bool rebuild();
  bool refreshChangedFiles();
  std::vector<SearchResult> search(std::string_view query, SearchScope scope = SearchScope::All) const;
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

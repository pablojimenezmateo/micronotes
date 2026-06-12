#pragma once

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
};

}

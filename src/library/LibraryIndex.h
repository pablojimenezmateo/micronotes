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

// A note as the index already holds it. Everything the note list needs, without
// opening the file a second time: the refresh has just read every file that
// changed and written these five fields to SQLite, so the note list is a
// `SELECT` where it used to be a second recursive walk of the library plus a
// re-open and a front-matter parse of every note in it.
struct IndexedNote {
  std::string id;
  std::filesystem::path relativePath;
  std::string title;
  std::vector<std::string> tags;
  std::string icon;
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
  bool isOpen() const;

  // Every indexed note, in no particular order. One statement, no file reads.
  std::vector<IndexedNote> notes() const;

  // Every directory under the root, library-relative, as the last refresh saw
  // them. The note list cannot name the *empty* folders and the sidebar tree
  // needs them, so somebody has to walk the tree for them -- and the refresh
  // already does, once, with the stat it needs for each file. Handing that
  // walk's directories out is what leaves the startup at one walk rather than
  // two.
  const std::vector<std::filesystem::path>& directories() const;

private:
  std::filesystem::path root_;
  std::filesystem::path dbPath_;
  std::vector<SearchResult> rows_;
  // Filled by every walk this class makes, so `directories()` never causes one.
  std::vector<std::filesystem::path> directories_;
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

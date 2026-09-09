#pragma once

#include "CoreAliases.h"

#include "core/persistence/SqliteDb.h"
#include "library/Library.h"
#include "library/Metadata.h"

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

  // What re-indexing one file found.
  //
  // `listFieldsChanged` reports whether any of the five fields the *note list*
  // is built from -- id, path, title, tags, icon -- came out different. It is
  // false for the ordinary save, which changes only the body, and that is the
  // whole point of it: the sidebar, the folder counts, the tag list and the
  // note-by-id map are all derived from those five, so a false answer means
  // none of them has to be thrown away and rebuilt.
  struct FileRefresh {
    bool ok = false;
    bool listFieldsChanged = false;
    // Whether any row was written at all. False when the file's stat still
    // matches the row, which is the answer for every echo of micronotes' own
    // save coming back through the watcher -- and what lets the caller skip
    // bumping the library revision, and so skip rebuilding the view memos
    // keyed on it, for a change that was not one.
    bool rowsWritten = false;
  };

  // Re-indexes exactly one file: no tree walk, no whole-table read, no
  // transaction unless something actually changed.
  //
  // This is what a save wants. `refreshChangedFiles` exists to *discover* what
  // changed, and paid a recursive walk plus a stat per note plus a read of
  // every row in the table to do it; a save already knows which single file it
  // just wrote. On a 1,000-note library that discovery was the entire cost of
  // an autosave.
  //
  // A path that no longer exists has its rows removed, so this is also the
  // right call after a delete or a move -- once for each end of the move.
  FileRefresh refreshFile(const std::filesystem::path& absolutePath);

  // The same refresh, for a caller that has *just written* the file and so
  // already holds everything the index wants from it: the front matter it
  // wrote and the body under it.
  //
  // `refreshFile` would open the note micronotes wrote milliseconds ago and
  // parse it back apart -- for a 200 KB note that is a 200 KB read plus three
  // copies of it (the file, the stripped body, the row), on the autosave path,
  // to recover bytes the caller is still holding.
  FileRefresh refreshWrittenFile(const std::filesystem::path& absolutePath,
                                 const NoteMetadata& metadata, std::string_view body);

  std::vector<SearchResult> search(std::string_view query, SearchScope scope = SearchScope::All) const;

  // Every note whose text carries a `[[target]]` naming this one.
  //
  // The stored target is the text as written, and matching happens here rather
  // than at index time. That is deliberate: an index of resolved ids would have
  // to be found and rewritten on every rename, whereas matching at query time
  // means a rename changes what resolves without touching a single row.
  // `title` and `stem` are the two spellings a link is allowed to use.
  std::vector<Backlink> backlinks(std::string_view title, std::string_view stem) const;
  // How many notes the index holds. One `count(*)` over the primary key.
  std::size_t size() const;
  bool isOpen() const;

  // Whether the full-text table keeps its own copy of every note's text.
  //
  // False wherever SQLite is new enough for a contentless fts5 table that can
  // still delete a row (3.43), which is the shape `migrate` asks for: the index
  // then holds the terms and not the text, and a 10.3 MB library indexes to
  // 15.5 MB rather than 27.2 MB. True on the fallback, which is what every
  // build wrote before and is not a second mode -- every statement the index
  // runs means the same thing on both, and this is the only question that can
  // tell them apart.
  bool ftsStoresBodies() const;

  // Every indexed note, in no particular order. One statement, no file reads.
  std::vector<IndexedNote> notes() const;

  // Every directory under the root, library-relative, as the last refresh saw
  // them. The note list cannot name the *empty* folders and the sidebar tree
  // needs them, so somebody has to walk the tree for them -- and the refresh
  // already does, once, with the stat it needs for each file. Handing that
  // walk's directories out is what leaves the startup at one walk rather than
  // two.
  const std::vector<std::filesystem::path>& directories() const;
  // Every companion entry the last refresh's walk passed, for the same reason
  // the directories are here: the walk is already paid for. None of it is in
  // SQLite -- a companion is listed and named, never indexed.
  const std::vector<CompanionEntry>& companions() const;

private:
  // The body both refreshes share. `written` is the front matter and body the
  // caller already holds, or null when they have to be read off the disk.
  struct WrittenNote {
    const NoteMetadata* metadata = nullptr;
    std::string_view body;
  };
  FileRefresh refreshPath(const std::filesystem::path& absolutePath, const WrittenNote* written);

  std::filesystem::path root_;
  std::filesystem::path dbPath_;
  // Filled by every walk this class makes, so `directories()` never causes one.
  std::vector<std::filesystem::path> directories_;
  std::vector<CompanionEntry> companions_;
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

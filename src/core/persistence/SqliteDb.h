#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sqlite3.h>

struct sqlite3;
struct sqlite3_stmt;

namespace microcore::persistence {

class SqliteDb;

// A prepared statement borrowed from the connection's cache.
//
// Destruction resets the statement and returns it to the cache rather than
// finalizing it, so a given SQL string is compiled once per connection instead
// of once per call. When the cached statement is already in use further up the
// stack -- one query stepping while a nested call wants the same SQL -- this
// falls back to compiling a private statement and finalizes that one on
// destruction, so nesting stays correct instead of silently resetting a
// statement someone else is still iterating.
class Statement {
public:
  // One cached statement plus whether a handle to it is currently outstanding.
  // Defined here so SqliteDb can hold a map of them.
  struct Slot {
    sqlite3_stmt* stmt = nullptr;
    // Set while a Statement handle is live, so a nested prepare of the same SQL
    // compiles its own rather than hijacking this one mid-iteration.
    bool inUse = false;
  };

  Statement() = default;
  ~Statement();

  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&& other) noexcept;
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  sqlite3_stmt* get() const { return stmt_; }
  operator sqlite3_stmt*() const { return stmt_; }
  explicit operator bool() const { return stmt_ != nullptr; }

private:
  friend class SqliteDb;
  Statement(sqlite3_stmt* stmt, Slot* slot) : stmt_(stmt), slot_(slot) {}

  void release();

  sqlite3_stmt* stmt_ = nullptr;
  // Non-null when the statement came from the cache: points straight at its
  // slot so releasing is O(1) rather than a scan of every cached statement.
  // unordered_map keeps element addresses stable, so this stays valid.
  Slot* slot_ = nullptr;
};

// One sqlite connection, held open for as long as its owner needs it.
//
// The pattern this replaces -- open a connection, run one statement, close it
// again -- is expensive in two ways that are easy to miss. Compiling SQL is not
// free, and it was paid on every call. More importantly, several of the
// settings that make sqlite both fast and correct are *per connection*, not
// stored in the database file: `synchronous` and `foreign_keys` reset to their
// defaults on every new connection. Applying them once during migration, on a
// connection that is then closed, leaves every subsequent statement running
// with synchronous=FULL (an fsync per commit) and foreign keys disabled -- so a
// schema's ON DELETE CASCADE never fires and children are orphaned silently.
class SqliteDb {
public:
  SqliteDb() = default;
  ~SqliteDb();
  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  // Opens the database and applies the per-connection pragmas. They MUST be set
  // here rather than in a migration step, because they do not persist.
  bool open(const std::filesystem::path& path);
  void close();

  // NUL-terminated because sqlite3_exec is: taking a view meant every call --
  // the BEGIN and the COMMIT of every save among them -- built a std::string
  // purely to get a terminator onto a string literal that already had one.
  bool exec(const char* sql);
  bool exec(const std::string& sql) { return exec(sql.c_str()); }

  // A statement ready for binding: reset, with bindings cleared. Compiled once
  // per distinct SQL string per connection.
  Statement prepare(std::string_view sql);

  bool isOpen() const { return db_ != nullptr; }
  sqlite3* handle() const { return db_; }

private:
  friend class Statement;

  // Transparently hashed, so probing the cache with a `string_view` does not
  // build a `std::string` from the SQL first. Every prepare paid that -- a heap
  // allocation and a copy of a statement up to 300 bytes long -- to look up a
  // statement whose whole purpose is to avoid work.
  struct SqlHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view sql) const {
      return std::hash<std::string_view> {}(sql);
    }
  };
  sqlite3* db_ = nullptr;
  std::unordered_map<std::string, Statement::Slot, SqlHash, std::equal_to<>> cache_;
};

// --- statement values -------------------------------------------------------
//
// Binding a parameter and reading a column back. They belong beside the
// statement rather than in whichever file happens to run the query: the two
// notes below are both sqlite traps rather than preferences, and a caller
// writing `sqlite3_bind_text` by hand has to know them already to get them
// right.

inline void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

// Binds without handing sqlite a copy to keep. The caller guarantees the bytes
// outlive the step, which for a note's body is worth the obligation: it is the
// largest thing in the row by three orders of magnitude, and SQLITE_TRANSIENT
// meant sqlite copied the whole note -- twice, once for the row and once for
// the fts entry -- on every save.
inline void bindTextBorrowed(sqlite3_stmt* stmt, int index, std::string_view value) {
  // An empty view's `data()` may be null, and sqlite reads a null pointer as
  // SQL NULL rather than as an empty string -- which a NOT NULL column then
  // rejects. An empty value is not a missing one.
  sqlite3_bind_text(stmt, index, value.empty() ? "" : value.data(),
                    static_cast<int>(value.size()), SQLITE_STATIC);
}

inline std::string columnText(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

// The same column as a view of sqlite's own buffer, which stays valid until the
// next step or reset of this statement. For a column the caller only reads:
// copying 200 note bodies out to look at three lines of one is the whole cost
// of a search.
//
// `sqlite3_column_bytes` must be called after `_text`, not before: it reports
// the length of the representation the last accessor produced, so asking it
// first can convert the value and give the length of the wrong encoding.
inline std::string_view columnView(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  if(!text) return {};
  const auto size = static_cast<std::size_t>(sqlite3_column_bytes(stmt, index));
  return std::string_view(reinterpret_cast<const char*>(text), size);
}

}

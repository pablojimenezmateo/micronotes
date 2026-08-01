#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

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

  bool exec(std::string_view sql);

  // A statement ready for binding: reset, with bindings cleared. Compiled once
  // per distinct SQL string per connection.
  Statement prepare(std::string_view sql);

  bool isOpen() const { return db_ != nullptr; }
  sqlite3* handle() const { return db_; }

  std::string lastError() const;

private:
  friend class Statement;

  sqlite3* db_ = nullptr;
  std::unordered_map<std::string, Statement::Slot> cache_;
};

}

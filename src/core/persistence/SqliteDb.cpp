#include "core/persistence/SqliteDb.h"

#include "core/perf/PerformanceCounters.h"

#include <sqlite3.h>

#include <utility>

namespace microcore::persistence {

Statement::~Statement() {
  release();
}

Statement::Statement(Statement&& other) noexcept
  : stmt_(other.stmt_), slot_(other.slot_) {
  other.stmt_ = nullptr;
  other.slot_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
  if(this != &other) {
    release();
    stmt_ = std::exchange(other.stmt_, nullptr);
    slot_ = std::exchange(other.slot_, nullptr);
  }
  return *this;
}

void Statement::release() {
  if(!stmt_) return;
  if(slot_) {
    // Reset now rather than at next use, so the statement stops holding its
    // read transaction open the moment the caller is done with it.
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    slot_->inUse = false;
  } else {
    // Private statement compiled because the cached one was busy; it is not
    // reusable by anyone else, so finalize it.
    sqlite3_finalize(stmt_);
  }
  stmt_ = nullptr;
  slot_ = nullptr;
}

SqliteDb::~SqliteDb() {
  close();
}

bool SqliteDb::open(const std::filesystem::path& path) {
  close();
  if(sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    close();
    return false;
  }
  perf::addCounter(perf::CounterId::SqliteConnectionOpens);

  // journal_mode is persisted in the file, but the other two are not: they are
  // per-connection and revert to synchronous=FULL and foreign_keys=OFF every
  // time a connection is made. Setting them anywhere other than here means
  // paying an fsync per commit and losing ON DELETE CASCADE entirely.
  exec("PRAGMA journal_mode=WAL;");
  exec("PRAGMA synchronous=NORMAL;");
  exec("PRAGMA foreign_keys=ON;");
  // Wait rather than return SQLITE_BUSY when another connection (a second
  // window, or the WAL checkpointer) holds the write lock.
  sqlite3_busy_timeout(db_, 5000);
  return true;
}

void SqliteDb::close() {
  for(auto& [sql, slot] : cache_) {
    if(slot.stmt) sqlite3_finalize(slot.stmt);
  }
  cache_.clear();
  if(db_) sqlite3_close(db_);
  db_ = nullptr;
}

bool SqliteDb::exec(std::string_view sql) {
  if(!db_) return false;
  perf::addCounter(perf::CounterId::SqliteExecCalls);
  char* error = nullptr;
  const int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &error);
  sqlite3_free(error);
  return rc == SQLITE_OK;
}

Statement SqliteDb::prepare(std::string_view sql) {
  if(!db_) return {};

  const auto found = cache_.find(std::string(sql));
  if(found != cache_.end() && !found->second.inUse) {
    found->second.inUse = true;
    sqlite3_reset(found->second.stmt);
    sqlite3_clear_bindings(found->second.stmt);
    return Statement(found->second.stmt, &found->second);
  }

  sqlite3_stmt* stmt = nullptr;
  perf::addCounter(perf::CounterId::SqliteStatementsPrepared);
  if(sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
    return {};
  }

  // The cached statement for this SQL is busy further up the stack, so this one
  // stays private to the caller and is finalized when their handle dies.
  if(found != cache_.end()) return Statement(stmt, nullptr);

  auto [entry, inserted] = cache_.emplace(std::string(sql), Statement::Slot {stmt, true});
  return Statement(stmt, &entry->second);
}

std::string SqliteDb::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "database is not open";
}

}

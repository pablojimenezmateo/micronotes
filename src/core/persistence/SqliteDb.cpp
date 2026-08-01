#include "core/persistence/SqliteDb.h"

#include "core/perf/PerformanceCounters.h"

#include <sqlite3.h>

namespace microcore::persistence {

SqliteDb::SqliteDb() = default;

SqliteDb::~SqliteDb() {
  close();
}

bool SqliteDb::open(const std::filesystem::path& path) {
  close();
  return sqlite3_open(path.c_str(), &db_) == SQLITE_OK;
}

void SqliteDb::close() {
  if(db_) sqlite3_close(db_);
  db_ = nullptr;
}

bool SqliteDb::exec(const std::string& sql) {
  if(!db_) return false;
  perf::addCounter(perf::CounterId::SqliteExecCalls);
  char* error = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
  sqlite3_free(error);
  return rc == SQLITE_OK;
}

bool SqliteDb::isOpen() const {
  return db_ != nullptr;
}

}

#pragma once

#include <filesystem>
#include <string>

struct sqlite3;

namespace micronotes::persistence {

class SqliteDb {
public:
  SqliteDb();
  ~SqliteDb();
  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  bool open(const std::filesystem::path& path);
  void close();
  bool exec(const std::string& sql);
  bool isOpen() const;

private:
  sqlite3* db_ = nullptr;
};

}

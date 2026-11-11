#include <sqlite3.h>
#include "third_party/sqlite/sqlite3.h"
int Open() { sqlite3* db = nullptr; return sqlite3_open(":memory:", &db) == SQLITE_OK; }

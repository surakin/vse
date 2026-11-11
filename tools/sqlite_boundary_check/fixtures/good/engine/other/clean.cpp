// This comment names sqlite3_open and SQLITE_OK and #include <sqlite3.h>, which is allowed in a comment.
/* so is a block comment about sqlite3_stmt */
#include "database/sqlite/sqlite_version.h"
const char* Text() { return "sqlite3_open is only a string here"; }
int SqliteVersionNumber() { return 3; }

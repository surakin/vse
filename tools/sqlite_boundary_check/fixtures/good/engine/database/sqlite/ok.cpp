#include <sqlite3.h>
int Version() { return sqlite3_libversion_number() + SQLITE_OK; }

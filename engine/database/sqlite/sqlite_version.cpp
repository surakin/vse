#include "database/sqlite/sqlite_version.h"

#include <sqlite3.h>

#include "database/sqlite/sqlite_os.h"

namespace vse::database
{

const char* SqliteVersion()
{
    // The version needs no initialization, but asking for it initializes: that links the object that defines
    // sqlite3_os_init (see sqlite_os.h).
    if (sqlite_detail::InitializeSqlite() != SQLITE_OK)
    {
        return nullptr;
    }
    return sqlite3_libversion();
}

}  // namespace vse::database

#include "database/sqlite/sqlite_os.h"
#include "database/sqlite/sqlite_vfs.h"

#include <sqlite3.h>

#include "containers/unique_ptr.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

// SQLITE_OS_OTHER (third_party/sqlite/VENDORED.md): SQLite has no operating-system layer of its own. This file holds
// the mutex methods (the engine's RecursiveMutex) and the two hooks SQLite calls from sqlite3_initialize and
// sqlite3_shutdown; the virtual file system is sqlite_vfs.cpp.

struct sqlite3_mutex
{
    vse::RecursiveMutex mutex;
};

namespace vse::database::sqlite_detail
{
namespace
{

// --- mutexes
// ---------------------------------------------------------------------------------------------------------------------

// SQLite's static mutexes are types 2 to 13. Every mutex here is recursive: a superset of what SQLite asks of its fast
// mutexes.
constexpr int kFirstStaticMutex = SQLITE_MUTEX_STATIC_MAIN;
constexpr int kStaticMutexCount = 12;

sqlite3_mutex* StaticMutexes()
{
    static sqlite3_mutex mutexes[kStaticMutexCount];
    return mutexes;
}

int MutexInit()
{
    return SQLITE_OK;
}
int MutexEnd()
{
    return SQLITE_OK;
}

sqlite3_mutex* MutexAlloc(int type)
{
    if (type == SQLITE_MUTEX_FAST || type == SQLITE_MUTEX_RECURSIVE)
    {
        return MakeUnique<sqlite3_mutex>().Release();
    }
    if (type < kFirstStaticMutex || type >= kFirstStaticMutex + kStaticMutexCount)
    {
        return nullptr;
    }
    return &StaticMutexes()[type - kFirstStaticMutex];
}

void MutexFree(sqlite3_mutex* mutex)
{
    UniquePtr<sqlite3_mutex> release(mutex);
}
void MutexEnter(sqlite3_mutex* mutex)
{
    mutex->mutex.Lock();
}
int MutexTry(sqlite3_mutex* mutex)
{
    return mutex->mutex.TryLock() ? SQLITE_OK : SQLITE_BUSY;
}
void MutexLeave(sqlite3_mutex* mutex)
{
    mutex->mutex.Unlock();
}

int InitializeOnce()
{
    sqlite3_mutex_methods methods = {};
    methods.xMutexInit = MutexInit;
    methods.xMutexEnd = MutexEnd;
    methods.xMutexAlloc = MutexAlloc;
    methods.xMutexFree = MutexFree;
    methods.xMutexEnter = MutexEnter;
    methods.xMutexTry = MutexTry;
    methods.xMutexLeave = MutexLeave;
    // xMutexHeld and xMutexNotheld are only called from assert() in a SQLITE_DEBUG build of the library, which this is
    // not.
    const int rc = sqlite3_config(SQLITE_CONFIG_MUTEX, &methods);
    if (rc != SQLITE_OK)
    {
        return rc;
    }
    return sqlite3_initialize();
}

}  // namespace

namespace
{
Atomic<unsigned long long> g_prepared_statements{0};
}  // namespace

void NotePreparedStatement()
{
    g_prepared_statements.FetchAdd(1, MemoryOrder::Relaxed);
}

unsigned long long PreparedStatementCount()
{
    return g_prepared_statements.Load(MemoryOrder::Relaxed);
}

int InitializeSqlite()
{
    static const int result = InitializeOnce();
    return result;
}

}  // namespace vse::database::sqlite_detail

extern "C" int sqlite3_os_init()
{
    return sqlite3_vfs_register(vse::database::sqlite_detail::EngineVfs(), 1);
}
extern "C" int sqlite3_os_end()
{
    return sqlite3_vfs_unregister(vse::database::sqlite_detail::EngineVfs());
}

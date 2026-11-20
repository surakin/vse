#pragma once

// A directory made for one test and removed with everything in it afterwards, through the file layer (the database
// tests of Phase 19 keep real files).

#include "io/file_system.h"
#include "io/path.h"
#include "os/console.h"

namespace vse::database_test
{

class TempDir
{
public:
    TempDir()
    {
        io::Path tmp;
        if (io::TempDirectory(&tmp).Failed() || io::CreateTempDirectory(tmp, "re_db_test", &path_).Failed())
        {
            os::Abort();
        }
    }
    ~TempDir() { io::RemoveAll(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }
    io::Path operator/(StringView name) const { return path_.Join(name); }

private:
    io::Path path_;
};

}  // namespace vse::database_test

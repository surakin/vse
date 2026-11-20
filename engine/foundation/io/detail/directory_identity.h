#pragma once

#include "io/error.h"
#include "io/path.h"
#include "types.h"

// Internal to the file layer (Phase 13 Day 152): what the neutral directory walk needs from a backend to detect a loop.
// Not public API.
namespace vse::io::detail
{

/// Which object a directory is, so a walk can tell when a link leads back to an ancestor: device and inode on POSIX,
/// volume serial and file index on Windows.
struct Identity
{
    u64 device;
    u64 inode;
};

/// Fills `out` for the directory `path`, following links. Implemented per platform (io/<platform>/directory.cpp).
Status GetIdentity(const Path& path, Identity* out);

}  // namespace vse::io::detail

#pragma once

// The part of the Windows post-mortem backend (post_mortem.cpp) the tests reach directly: reading a LocalDumps key from
// any registry root, so a test can check it against a key of its own under HKCU instead of the machine's. Not part of
// the public API.
#include "debug/post_mortem.h"
#include "os/windows/win32.h"
#include "types.h"

namespace engine_placeholder::debug::detail
{

// Reads the LocalDumps values WER would use for the program `exe_name` under `root\local_dumps_key` (its own subkey,
// else the key itself) into `report->destination`, the limits and `*dump_type`; false, with `destination` and `note`
// saying what to set, when neither exists.
bool DescribeLocalDumps(HKEY root, const wchar_t* local_dumps_key, const wchar_t* exe_name, PostMortemReport* report,
                        u32* dump_type);

}  // namespace engine_placeholder::debug::detail

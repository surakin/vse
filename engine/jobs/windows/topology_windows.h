#pragma once

#include "types.h"

// The Windows CPU list, as a function of the data the OS returns, so a test can feed it recorded buffers (several processor groups) on a
// machine with one group. Internal to engine/jobs (Phase 13 Day 153).
namespace engine_placeholder::jobs::detail {

/// Turns a GetLogicalProcessorInformationEx(RelationGroup) buffer into CPU ids `group * 64 + bit`, ascending, for the groups in
/// `process_groups` (the process's groups, GetProcessGroupAffinity). With one process group, `primary_mask` (the process affinity mask,
/// GetProcessAffinityMask) also restricts that group, so `start /affinity` and job objects are honoured.
///
/// Param buffer, size: the RelationGroup records; a malformed record (a size that does not fit) ends the parse.
/// - Returns: the number of ids written to `out` (at most `capacity`).
usize CpusFromGroupRecords(const void* buffer, usize size, const u16* process_groups, usize group_count, u64 primary_mask, u32* out,
                           usize capacity);

} // namespace engine_placeholder::jobs::detail

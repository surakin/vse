#pragma once

// The prelude of every non-standalone documentation sample (docs/DOC-CONVENTIONS.md,
// "Samples"): the commonly used engine headers and the engine namespace, so a
// snippet in a guide can start with the code that matters. A sample that needs
// anything else says so with its own #include inside a `standalone` block.

#include <cstdio>
#include <cstdlib>

#include "containers/array.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "int128.h"
#include "algorithm.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "containers/hash_map.h"
#include "io/file.h"
#include "io/directory.h"
#include "io/file_system.h"
#include "io/path.h"
#include "jobs/async_io.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "log/log.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/transform.h"
#include "math/vector.h"
#include "mem/arena_allocator.h"
#include "mem/pool_allocator.h"
#include "mem/stack_allocator.h"
#include "string/string.h"
#include "types.h"

using namespace engine_placeholder;

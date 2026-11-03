// Compiles job.h under the production flags (-fno-exceptions -fno-rtti)
// so a break in the coroutine machinery under those flags is caught here
// rather than when the scheduler first includes the header.
#include "jobs/job.h"

namespace engine_placeholder::jobs {
namespace {
[[maybe_unused]] Job CompileCheck() { co_return; }
} // namespace
} // namespace engine_placeholder::jobs

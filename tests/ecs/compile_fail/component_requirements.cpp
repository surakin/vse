// Compiled by tests/ecs/CMakeLists.txt once per CASE: CASE 0 must compile (the control), every other case must be
// refused.
#include "ecs/component.h"

using namespace engine_placeholder::ecs;

namespace
{
struct Plain
{
    int x;
};
struct NoMove
{
    NoMove() = default;
    NoMove(NoMove&&) = delete;
};
class PrivateDestructor
{
public:
    PrivateDestructor() = default;
    PrivateDestructor(PrivateDestructor&&) = default;

private:
    ~PrivateDestructor() = default;
};
struct alignas(128) OverAligned
{
    int x;
};
}  // namespace

#if CASE == 1
using Bad = Plain&;
#elif CASE == 2
using Bad = Plain[3];
#elif CASE == 3
using Bad = const Plain;
#elif CASE == 4
using Bad = volatile Plain;
#elif CASE == 5
using Bad = NoMove;
#elif CASE == 6
using Bad = OverAligned;
#elif CASE == 7
using Bad = PrivateDestructor;
#else
using Bad = Plain;
#endif

int Probe()
{
    return ComponentOf<Bad>() == kInvalidComponent ? 1 : 0;
}

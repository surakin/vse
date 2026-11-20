// Compiled by tests/ecs/CMakeLists.txt once per CASE: CASE 0 must compile (the control), every other case must be
// refused.
#include "ecs/query.h"

using namespace vse::ecs;

namespace
{
struct A
{
    int v;
};
struct B
{
    int v;
};
struct Tag
{
};
template <int N> struct C
{
    int v;
};
struct NotATerm
{
};
}  // namespace

#if CASE == 0
using Q = Query<Write<A>, Read<B>, Without<Tag>, With<C<0>>, Optional<C<1>>, OptionalWrite<C<2>>>;
#elif CASE == 1
using Q = Query<Read<A>, Write<A>>;  // the same component twice: Read and Write contradict
#elif CASE == 2
using Q = Query<With<Tag>, Without<Tag>>;
#elif CASE == 3
using Q = Query<Read<A>, Read<A>>;
#elif CASE == 4
using Q = Query<Read<Tag>>;  // a tag has no data
#elif CASE == 5
using Q = Query<Optional<A>, With<A>>;
#elif CASE == 6
using Q = Query<NotATerm>;
#elif CASE == 7
using Q = Query<>;  // no terms
#elif CASE == 8
using Q = Query<Read<C<0>>, Read<C<1>>, Read<C<2>>, Read<C<3>>, Read<C<4>>, Read<C<5>>, Read<C<6>>, Read<C<7>>,
                Read<C<8>>, Read<C<9>>, Read<C<10>>, Read<C<11>>, Read<C<12>>, Read<C<13>>, Read<C<14>>, Read<C<15>>,
                Read<C<16>>>;  // 17 terms
#endif

#if CASE == 9
using Q = Query<Write<A>, Read<B>>;
#endif

// Naming the type is not enough to instantiate the class; taking its size is.
static_assert(sizeof(Q) > 0);

#if CASE == 9
// A function that does not take the data terms in order is refused where ForEach is called.
void WrongSignature(vse::ecs::World& world)
{
    auto q = world.Query<Write<A>, Read<B>>();
    q.ForEach([](const B&, A&) {});
}
#endif

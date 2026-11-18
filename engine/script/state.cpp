#include "script/backend.h"

#include "log/assert.h"

namespace engine_placeholder::script
{

// Out of line: ENGINE_VERIFY depends on the translation unit's debug-check setting, so it does not appear in a header.

void State::Acquire()
{
    ENGINE_VERIFY(owner_.Load(MemoryOrder::Acquire) == kNoOwner,
                  "script::State::Acquire: the state is owned by a thread; hand it over first");
    owner_.Store(CallerToken(), MemoryOrder::Release);
}

void State::CheckOwner() const
{
    ENGINE_VERIFY(OwnedByCaller(),
                  "script::State: used from a thread that does not own it (hand it over and acquire it)");
}

}  // namespace engine_placeholder::script

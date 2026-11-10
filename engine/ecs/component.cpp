#include "ecs/component.h"

#include "log/assert.h"
#include "sync/mutex.h"

namespace engine_placeholder::ecs {

ComponentRegistry& ComponentRegistry::Global() {
    static ComponentRegistry registry;
    return registry;
}

ComponentId ComponentRegistry::Register(const TypeInfo& info) {
    LockGuard<Mutex> guard(lock_);
    const u32 n = count_.Load(MemoryOrder::Relaxed);
    if (n >= kMaxComponentTypes) {
        refused_.FetchAdd(1, MemoryOrder::Relaxed);
        return kInvalidComponent;
    }
    TypeInfo& entry = entries_[n];
    entry = info;
    if (entry.name == nullptr) {
        char* out = names_[n];
        const char prefix[] = "component#";
        u32 length = 0;
        for (; prefix[length] != '\0'; ++length) {
            out[length] = prefix[length];
        }
        char digits[4];
        u32 digit_count = 0;
        u32 value = n;
        do {
            digits[digit_count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (digit_count != 0) {
            out[length++] = digits[--digit_count];
        }
        out[length] = '\0';
        entry.name = out;
    }
    count_.Store(n + 1, MemoryOrder::Release);
    return static_cast<ComponentId>(n);
}

const TypeInfo& ComponentRegistry::Info(ComponentId id) const {
    ENGINE_ASSERT(id < count_.Load(MemoryOrder::Acquire), "ComponentRegistry::Info: the id is not registered");
    return entries_[id];
}

u32 ComponentRegistry::Count() const {
    return count_.Load(MemoryOrder::Acquire);
}

u64 ComponentRegistry::Refused() const {
    return refused_.Load(MemoryOrder::Relaxed);
}

} // namespace engine_placeholder::ecs

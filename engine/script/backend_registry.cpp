#include "script/backend_registry.h"

#include "cstr/cstr.h"
#include "sync/mutex.h"

namespace vse::script
{

namespace
{

struct Table
{
    Mutex lock;
    BackendDescriptor entries[kMaxBackends];
    usize count = 0;
};

Table& GetTable()
{
    static Table table;
    return table;
}

bool SameName(const char* a, StringView b)
{
    return StringView(a, CStrLength(a)) == b;
}

}  // namespace

bool RegisterBackend(const BackendDescriptor& descriptor)
{
    if (descriptor.name == nullptr || descriptor.create == nullptr || CStrLength(descriptor.name) == 0 ||
        CStrLength(descriptor.name) > 31)
    {
        return false;
    }
    Table& table = GetTable();
    LockGuard guard(table.lock);
    for (usize i = 0; i < table.count; ++i)
    {
        if (SameName(table.entries[i].name, StringView(descriptor.name, CStrLength(descriptor.name))))
        {
            return table.entries[i].create == descriptor.create;
        }
    }
    if (table.count >= kMaxBackends)
    {
        return false;
    }
    table.entries[table.count++] = descriptor;
    return true;
}

const BackendDescriptor* FindBackend(StringView name)
{
    Table& table = GetTable();
    LockGuard guard(table.lock);
    for (usize i = 0; i < table.count; ++i)
    {
        if (SameName(table.entries[i].name, name))
        {
            return &table.entries[i];
        }
    }
    return nullptr;
}

usize BackendCount()
{
    Table& table = GetTable();
    LockGuard guard(table.lock);
    return table.count;
}

const BackendDescriptor* BackendAt(usize index)
{
    Table& table = GetTable();
    LockGuard guard(table.lock);
    return index < table.count ? &table.entries[index] : nullptr;
}

UniquePtr<Backend> CreateBackend(StringView name, Outcome* error)
{
    const BackendDescriptor* descriptor = FindBackend(name);
    if (descriptor == nullptr)
    {
        if (error != nullptr)
        {
            String<HeapAllocator> message(HeapAllocator::Default(), "no script backend named '");
            message.Append(name);
            message.Append("'; this program has:");
            const usize count = BackendCount();
            for (usize i = 0; i < count; ++i)
            {
                message.Append(' ');
                message.Append(StringView(BackendAt(i)->name, CStrLength(BackendAt(i)->name)));
            }
            *error = Outcome(Status::Invalid, message.View());
        }
        return UniquePtr<Backend>();
    }
    if (error != nullptr)
    {
        *error = Outcome();
    }
    return descriptor->create();
}

void ClearBackendRegistryForTest()
{
    Table& table = GetTable();
    LockGuard guard(table.lock);
    table.count = 0;
    for (BackendDescriptor& entry : table.entries)
    {
        entry = BackendDescriptor();
    }
}

}  // namespace vse::script

#include "script/script_world.h"

#include "log/assert.h"
#include "os/os.h"
#include "sync/thread.h"

namespace engine_placeholder::script
{

struct PartitionSlot
{
    // The installation holds records the state's native functions point at: declared first, destroyed last.
    UniquePtr<Installation> installation;
    UniquePtr<State> state;
    // Used only by the thread that holds the partition.
    usize loaded = 0;
    bool module_host = false;  // whether the module host is loaded in the state
    // A module that failed to define or run leaves the state half loaded: every later checkout reports the same failure
    // until the state is reset (a retry would say "defined twice" instead).
    bool module_failed = false;
    Status module_failure_status = Status::Ok;
    String<HeapAllocator> module_failure{HeapAllocator::Default()};
    u32 index = 0;
    // 0 when free, else a token of the holding thread.
    Atomic<usize> owner{0};
    Atomic<u64> calls{0};
    Atomic<u64> nanoseconds{0};
    Atomic<u64> budget_hits{0};
    Atomic<u64> out_of_memory{0};
    Atomic<u64> resets{0};
    Atomic<u64> memory_bytes{0};
};

// The instruction budget of the top level of an entry module (a trusted state takes none and runs it unbudgeted).
constexpr u64 kModuleRunInstructions = 200'000'000;

u32 PartitionOf(u64 stable_id, u32 partitions)
{
    ENGINE_ASSERT(partitions != 0, "PartitionOf: the partition count is 0");
    // The splitmix64 finalizer: fixed constants, no platform or library dependence.
    u64 z = stable_id + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return static_cast<u32>(z % partitions);
}

PartitionLease::~PartitionLease()
{
    Release();
}

PartitionLease::PartitionLease(PartitionLease&& other) noexcept : world_(other.world_), slot_(other.slot_)
{
    other.world_ = nullptr;
    other.slot_ = nullptr;
}

PartitionLease& PartitionLease::operator=(PartitionLease&& other) noexcept
{
    if (this != &other)
    {
        Release();
        world_ = other.world_;
        slot_ = other.slot_;
        other.world_ = nullptr;
        other.slot_ = nullptr;
    }
    return *this;
}

u32 PartitionLease::Partition() const
{
    ENGINE_ASSERT(slot_ != nullptr, "PartitionLease::Partition: the lease is empty");
    return slot_->index;
}

State* PartitionLease::GetState() const
{
    ENGINE_ASSERT(slot_ != nullptr, "PartitionLease::GetState: the lease is empty");
    return slot_->state.Get();
}

Outcome PartitionLease::Call(StringView function_path, Span<const Value> args, const Budget& budget, CallOutput* out)
{
    ENGINE_ASSERT(slot_ != nullptr, "PartitionLease::Call: the lease is empty");
    return world_->CallIn(slot_, function_path, args, budget, out);
}

Outcome PartitionLease::Resume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out)
{
    ENGINE_ASSERT(slot_ != nullptr, "PartitionLease::Resume: the lease is empty");
    return world_->ResumeIn(slot_, coroutine, values, budget, out);
}

void PartitionLease::Release()
{
    if (slot_ != nullptr)
    {
        world_->ReleaseSlot(slot_);
        slot_ = nullptr;
        world_ = nullptr;
    }
}

ScriptWorld::ScriptWorld(const ScriptWorldConfig& config) : config_(config)
{
    ENGINE_ASSERT(config.backend != nullptr, "ScriptWorld: no backend");
    ENGINE_ASSERT(config.partitions >= 1 && config.partitions <= kMaxPartitions,
                  "ScriptWorld: partitions out of range");
    partition_count_ = config.partitions;
    slots_.Reserve(partition_count_);
    for (u32 i = 0; i < partition_count_; ++i)
    {
        auto slot = MakeUnique<PartitionSlot>();
        slot->index = i;
        slots_.Push(Move(slot));
    }
}

ScriptWorld::~ScriptWorld() = default;

void ScriptWorld::AddScript(StringView name, SharedBytesPtr source)
{
    LockGuard<Mutex> lock(scripts_mutex_);
    scripts_.Push(Script(name, Move(source)));
}

void ScriptWorld::AddModule(StringView key, StringView name, SharedBytesPtr bytes, usize offset, usize size,
                            StringView deps, bool entry)
{
    Script script(name, Move(bytes));
    script.key = String<HeapAllocator>(HeapAllocator::Default(), key);
    script.deps = String<HeapAllocator>(HeapAllocator::Default(), deps);
    script.offset = offset;
    script.size = size;
    script.entry = entry;
    LockGuard<Mutex> lock(scripts_mutex_);
    scripts_.Push(Move(script));
}

namespace
{

// Records a module failure in the slot and returns it.
Outcome FailModule(PartitionSlot* slot, const Outcome& failure)
{
    slot->module_failed = true;
    slot->module_failure_status = failure.status;
    slot->module_failure = String<HeapAllocator>(HeapAllocator::Default(), failure.message.View());
    return Outcome(failure.status, failure.message.View());
}

}  // namespace

Outcome ScriptWorld::PrepareState(PartitionSlot* slot)
{
    if (slot->state.Get() == nullptr)
    {
        slot->state = config_.backend->CreateState(config_.partition_memory);
        if (slot->state.Get() == nullptr)
        {
            return Outcome(Status::OutOfMemory, "the partition's state could not be made");
        }
        slot->loaded = 0;
        slot->module_host = false;
        slot->module_failed = false;
        if (config_.bindings != nullptr)
        {
            Outcome error;
            slot->installation = config_.bindings->Install(slot->state.Get(), config_.profile, &error);
            if (slot->installation.Get() == nullptr)
            {
                slot->state.Reset();
                return error;
            }
        }
    }
    if (slot->module_failed)
    {
        return Outcome(slot->module_failure_status, slot->module_failure.View());
    }
    // Catch up with the scripts added since this state last loaded: copy the shares under the lock, load without it.
    Vector<Script> pending;
    {
        LockGuard<Mutex> lock(scripts_mutex_);
        for (usize i = slot->loaded; i < scripts_.Size(); ++i)
        {
            Script copy(scripts_[i].name.View(), scripts_[i].source);
            copy.key = String<HeapAllocator>(HeapAllocator::Default(), scripts_[i].key.View());
            copy.deps = String<HeapAllocator>(HeapAllocator::Default(), scripts_[i].deps.View());
            copy.offset = scripts_[i].offset;
            copy.size = scripts_[i].size;
            copy.entry = scripts_[i].entry;
            pending.Push(Move(copy));
        }
    }
    for (usize i = 0; i < pending.Size(); ++i)
    {
        const ByteSpan bytes = pending[i].source->Bytes();
        if (pending[i].key.Length() != 0)
        {
            if (pending[i].offset > bytes.Size() || pending[i].size > bytes.Size() - pending[i].offset)
            {
                return Outcome(Status::Invalid, "a module's text lies outside its buffer");
            }
            if (!slot->module_host)
            {
                const Outcome host = slot->state->Load(StringView("module_host"), StringView(LuaModuleHostSource()));
                if (!host.IsOk())
                {
                    return Outcome(host.status, host.message.View());
                }
                slot->module_host = true;
            }
            const StringView text(reinterpret_cast<const char*>(bytes.Data()) + pending[i].offset, pending[i].size);
            const Outcome defined = slot->state->DefineModule(pending[i].key.View(), pending[i].name.View(), text,
                                                              pending[i].deps.View());
            if (!defined.IsOk())
            {
                return FailModule(slot, defined);
            }
            if (pending[i].entry)
            {
                Value key[1] = {Value::FromText(pending[i].key.View())};
                CallOutput out;
                Budget budget;
                budget.instructions = kModuleRunInstructions;
                Outcome ran = slot->state->Call(StringView("__module_run"), Span<const Value>(key, 1), budget, &out);
                if (ran.status == Status::Unsupported)  // a trusted state takes no instruction budget
                {
                    ran = slot->state->Call(StringView("__module_run"), Span<const Value>(key, 1), Budget(), &out);
                }
                if (!ran.IsOk())
                {
                    return FailModule(slot, ran);
                }
                if (out.Suspended())
                {  // a script that waits while it loads would stay half run
                    slot->state->Abandon(out.coroutine);
                    return FailModule(slot, Outcome(Status::RuntimeError, "a script cannot wait while it loads"));
                }
            }
            ++slot->loaded;
            continue;
        }
        const Outcome loaded = slot->state->Load(pending[i].name.View(),
                                                 StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
        if (!loaded.IsOk())
        {
            return Outcome(loaded.status, loaded.message.View());
        }
        ++slot->loaded;
    }
    return Outcome();
}

CheckoutStatus ScriptWorld::TryCheckout(u32 partition, PartitionLease* lease, Outcome* error)
{
    ENGINE_ASSERT(lease != nullptr, "TryCheckout: no lease to fill");
    if (partition >= partition_count_)
    {
        if (error != nullptr)
        {
            *error = Outcome(Status::Invalid, "the partition number is out of range");
        }
        return CheckoutStatus::Invalid;
    }
    PartitionSlot* slot = slots_[partition].Get();
    usize expected = 0;
    const usize token = ThisThread::Id().Hash() | 1u;
    if (!slot->owner.CompareExchangeStrong(expected, token, MemoryOrder::AcqRel, MemoryOrder::Acquire))
    {
        return CheckoutStatus::Busy;
    }
    if (slot->state.Get() != nullptr)
    {
        slot->state->Acquire();
    }
    const Outcome prepared = PrepareState(slot);
    if (!prepared.IsOk())
    {
        if (slot->state.Get() != nullptr)
        {
            slot->state->HandOver();
        }
        slot->owner.Store(0, MemoryOrder::Release);
        if (error != nullptr)
        {
            *error = Outcome(prepared.status, prepared.message.View());
        }
        return CheckoutStatus::Failed;
    }
    *lease = PartitionLease(this, slot);
    return CheckoutStatus::Ok;
}

CheckoutStatus ScriptWorld::Checkout(u32 partition, PartitionLease* lease, Outcome* error)
{
    const CheckoutStatus status = TryCheckout(partition, lease, error);
    ENGINE_ASSERT(status != CheckoutStatus::Busy, "ScriptWorld::Checkout: the partition is held by another job");
    return status;
}

void ScriptWorld::ReleaseSlot(PartitionSlot* slot)
{
    if (slot->state.Get() != nullptr)
    {
        slot->memory_bytes.Store(slot->state->MemoryUsed(), MemoryOrder::Relaxed);
        slot->state->HandOver();
    }
    else
    {
        slot->memory_bytes.Store(0, MemoryOrder::Relaxed);
    }
    slot->owner.Store(0, MemoryOrder::Release);
}

Outcome ScriptWorld::CallIn(PartitionSlot* slot, StringView path, Span<const Value> args, const Budget& budget,
                            CallOutput* out)
{
    if (slot->state.Get() == nullptr)
    {
        const Outcome prepared = PrepareState(slot);
        if (!prepared.IsOk())
        {
            return Outcome(prepared.status, prepared.message.View());
        }
    }
    const u64 start = os::MonotonicTimeNanos();
    Outcome result = slot->state->Call(path, args, budget, out);
    return Settle(slot, Move(result), start);
}

Outcome ScriptWorld::ResumeIn(PartitionSlot* slot, u64 coroutine, Span<const Value> values, const Budget& budget,
                              CallOutput* out)
{
    if (slot->state.Get() == nullptr)
    {
        return Outcome(Status::Invalid, "the partition has no state: the coroutine is gone");
    }
    const u64 start = os::MonotonicTimeNanos();
    Outcome result = slot->state->Resume(coroutine, values, budget, out);
    return Settle(slot, Move(result), start);
}

Outcome ScriptWorld::Settle(PartitionSlot* slot, Outcome result, u64 start)
{
    slot->nanoseconds.FetchAdd(os::MonotonicTimeNanos() - start, MemoryOrder::Relaxed);
    slot->calls.FetchAdd(1, MemoryOrder::Relaxed);
    if (result.status == Status::BudgetExceeded)
    {
        slot->budget_hits.FetchAdd(1, MemoryOrder::Relaxed);
    }
    else if (result.status == Status::OutOfMemory)
    {
        slot->out_of_memory.FetchAdd(1, MemoryOrder::Relaxed);
        if (config_.reset_on_out_of_memory)
        {
            // A fresh state with the same scripts; the other partitions are not touched.
            slot->state.Reset();
            slot->installation.Reset();
            slot->resets.FetchAdd(1, MemoryOrder::Relaxed);
            PrepareState(slot);
        }
    }
    return result;
}

PartitionCounters ScriptWorld::Counters(u32 partition) const
{
    PartitionCounters c;
    if (partition >= partition_count_)
    {
        return c;
    }
    const PartitionSlot& s = *slots_[partition];
    c.calls = s.calls.Load(MemoryOrder::Relaxed);
    c.nanoseconds = s.nanoseconds.Load(MemoryOrder::Relaxed);
    c.budget_hits = s.budget_hits.Load(MemoryOrder::Relaxed);
    c.out_of_memory = s.out_of_memory.Load(MemoryOrder::Relaxed);
    c.resets = s.resets.Load(MemoryOrder::Relaxed);
    c.memory_bytes = s.memory_bytes.Load(MemoryOrder::Relaxed);
    return c;
}

PartitionCounters ScriptWorld::Total() const
{
    PartitionCounters t;
    for (u32 i = 0; i < partition_count_; ++i)
    {
        const PartitionCounters c = Counters(i);
        t.calls += c.calls;
        t.nanoseconds += c.nanoseconds;
        t.budget_hits += c.budget_hits;
        t.out_of_memory += c.out_of_memory;
        t.resets += c.resets;
        t.memory_bytes += c.memory_bytes;
    }
    return t;
}

}  // namespace engine_placeholder::script

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

// Calls a function of the module host, which may not wait: the budget is the module run's (a trusted state takes no
// instruction budget and runs unbudgeted).
Outcome RunHost(State* state, StringView path, Span<const Value> args, CallOutput* out_results = nullptr)
{
    CallOutput local;
    CallOutput* out = out_results != nullptr ? out_results : &local;
    Budget budget;
    budget.instructions = kModuleRunInstructions;
    Outcome ran = state->Call(path, args, budget, out);
    if (ran.status == Status::Unsupported)
    {
        ran = state->Call(path, args, Budget(), out);
    }
    if (ran.IsOk() && out->Suspended())
    {  // a script that waits while it loads would stay half run
        state->Abandon(out->coroutine);
        return Outcome(Status::RuntimeError, "a script cannot wait while it loads");
    }
    return ran;
}

// True when `outcome` says the function does not exist (an optional host function that is not loaded).
bool IsMissingFunction(const Outcome& outcome)
{
    return !outcome.IsOk() && outcome.message.View().Find(StringView("nil value")) < outcome.message.View().Length();
}

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
                const Outcome host = slot->state->LoadHost(StringView("module_host"),
                                                           StringView(LuaModuleHostSource()));
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
                const Outcome ran = RunHost(slot->state.Get(), StringView("@module_run"), key);
                if (!ran.IsOk())
                {
                    return FailModule(slot, ran);
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

Outcome ScriptWorld::ReloadModules(Span<const ModuleReplacement> replacements, ReloadResult* result)
{
    ENGINE_ASSERT(result != nullptr, "ReloadModules: no result");
    *result = ReloadResult();
    if (replacements.Size() == 0)
    {
        return Outcome();
    }
    // 1. Hold every partition that has a state, caught up with the scripts added so far.
    Vector<PartitionLease> held;
    for (u32 p = 0; p < partition_count_; ++p)
    {
        PartitionSlot* slot = slots_[p].Get();
        usize expected = 0;
        const usize token = ThisThread::Id().Hash() | 1u;
        if (!slot->owner.CompareExchangeStrong(expected, token, MemoryOrder::AcqRel, MemoryOrder::Acquire))
        {
            return Outcome(Status::Invalid, "a partition is busy: reload between ticks");
        }
        if (slot->state.Get() == nullptr)
        {
            slot->owner.Store(0, MemoryOrder::Release);
            continue;
        }
        slot->state->Acquire();
        PartitionLease lease(this, slot);
        const Outcome prepared = PrepareState(slot);
        if (!prepared.IsOk())
        {
            return Outcome(prepared.status, prepared.message.View());
        }
        if (slot->module_failed)
        {
            return Outcome(slot->module_failure_status, slot->module_failure.View());
        }
        held.Push(Move(lease));
    }
    // 2. Replace the texts in the list (the old entries are kept to put back) and find what runs again, in load order.
    struct Item
    {
        String<HeapAllocator> key{HeapAllocator::Default()};
        String<HeapAllocator> name{HeapAllocator::Default()};
        String<HeapAllocator> deps{HeapAllocator::Default()};
        SharedBytesPtr bytes;
        usize offset = 0;
        usize size = 0;
        bool replaced = false;
    };
    struct Backup
    {
        usize index = 0;
        SharedBytesPtr bytes;
        usize offset = 0;
        usize size = 0;
    };
    Vector<Item> items;
    Vector<Backup> backups;
    {
        LockGuard<Mutex> lock(scripts_mutex_);
        Vector<String<HeapAllocator>> selected;
        for (usize r = 0; r < replacements.Size(); ++r)
        {
            bool found = false;
            for (usize i = 0; i < scripts_.Size(); ++i)
            {
                found = found || scripts_[i].key.View() == replacements[r].key.View();
            }
            if (!found || replacements[r].bytes == nullptr ||
                replacements[r].offset > replacements[r].bytes->Bytes().Size() ||
                replacements[r].size > replacements[r].bytes->Bytes().Size() - replacements[r].offset)
            {
                return Outcome(Status::Invalid, "a replacement names no module or its text lies outside its buffer");
            }
        }
        for (usize i = 0; i < scripts_.Size(); ++i)
        {
            Script& script = scripts_[i];
            if (script.key.Length() == 0)
            {
                continue;
            }
            const ModuleReplacement* replacement = nullptr;
            for (usize r = 0; r < replacements.Size(); ++r)
            {
                if (replacements[r].key.View() == script.key.View())
                {
                    replacement = &replacements[r];  // the last one for a key wins
                }
            }
            bool depends = false;
            for (usize k = 0; k < selected.Size() && !depends; ++k)
            {
                const StringView deps = script.deps.View();
                usize pos = 0;
                while (pos <= deps.Length() && deps.Length() != 0)
                {
                    usize end = pos;
                    while (end < deps.Length() && deps[end] != '\n')
                    {
                        ++end;
                    }
                    depends = depends || deps.Substring(pos, end - pos) == selected[k].View();
                    pos = end + 1;
                }
            }
            if (replacement == nullptr && !depends)
            {
                continue;
            }
            selected.Push(String<HeapAllocator>(HeapAllocator::Default(), script.key.View()));
            Item item;
            item.key = String<HeapAllocator>(HeapAllocator::Default(), script.key.View());
            item.name = String<HeapAllocator>(HeapAllocator::Default(), script.name.View());
            item.deps = String<HeapAllocator>(HeapAllocator::Default(), script.deps.View());
            if (replacement != nullptr)
            {
                Backup backup;
                backup.index = i;
                backup.bytes = script.source;
                backup.offset = script.offset;
                backup.size = script.size;
                backups.Push(Move(backup));
                script.source = replacement->bytes;
                script.offset = replacement->offset;
                script.size = replacement->size;
                item.bytes = replacement->bytes;
                item.offset = replacement->offset;
                item.size = replacement->size;
                item.replaced = true;
            }
            items.Push(Move(item));
        }
    }
    // 3. Phase one, in every partition: run the new versions. Nothing is committed yet.
    auto restore = [&]()
    {
        for (usize h = 0; h < held.Size(); ++h)
        {
            State* state = held[h].GetState();
            CallOutput out;
            RunHost(state, StringView("@reload_abort"), Span<const Value>(), &out);
            RunHost(state, StringView("__behavior_reload_abort"), Span<const Value>(), &out);  // absent without it
        }
        LockGuard<Mutex> lock(scripts_mutex_);
        for (usize b = 0; b < backups.Size(); ++b)
        {
            scripts_[backups[b].index].source = backups[b].bytes;
            scripts_[backups[b].index].offset = backups[b].offset;
            scripts_[backups[b].index].size = backups[b].size;
        }
    };
    for (usize h = 0; h < held.Size(); ++h)
    {
        State* state = held[h].GetState();
        Outcome step = RunHost(state, StringView("@reload_begin"), Span<const Value>());
        if (step.IsOk())
        {
            const Outcome behavior = RunHost(state, StringView("__behavior_reload_begin"), Span<const Value>());
            if (!behavior.IsOk() && !IsMissingFunction(behavior))
            {
                step = Outcome(behavior.status, behavior.message.View());
            }
        }
        for (usize i = 0; step.IsOk() && i < items.Size(); ++i)
        {
            const byte* base = items[i].replaced ? items[i].bytes->Bytes().Data() : nullptr;
            if (items[i].replaced)
            {
                const StringView text(reinterpret_cast<const char*>(base) + items[i].offset, items[i].size);
                step = state->DefineModule(items[i].key.View(), items[i].name.View(), text, items[i].deps.View());
            }
            if (step.IsOk())
            {
                Value key[1] = {Value::FromText(items[i].key.View())};
                step = RunHost(state, StringView("@module_rerun"), key);
            }
        }
        if (!step.IsOk())
        {
            restore();
            return Outcome(step.status, step.message.View());
        }
    }
    // 4. Phase two: commit everywhere.
    for (usize h = 0; h < held.Size(); ++h)
    {
        State* state = held[h].GetState();
        RunHost(state, StringView("@reload_commit"), Span<const Value>());
        CallOutput out;
        const Outcome behavior = RunHost(state, StringView("__behavior_reload_commit"), Span<const Value>(), &out);
        if (behavior.IsOk() && out.results.Size() == 3)
        {
            result->instances_rebound += static_cast<u64>(out.results[0].AsInt());
            result->hook_errors += static_cast<u64>(out.results[1].AsInt());
            if (result->hook_error.Length() == 0 && out.results[2].Text().Length() != 0)
            {
                result->hook_error = String<HeapAllocator>(HeapAllocator::Default(), out.results[2].Text());
            }
        }
    }
    result->partitions = static_cast<u32>(held.Size());
    result->modules_run = static_cast<u32>(items.Size());
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

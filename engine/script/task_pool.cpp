#include "script/task_pool.h"
#include "containers/hash.h"

#include "containers/shared_bytes.h"
#include "jobs/parallel_for.h"
#include "log/assert.h"
#include "script/log_buffer.h"
#include "script/partition_context.h"
#include "sync/atomic.h"

namespace vse::script
{

// The host every task state loads first. `__task_define` stores a function by key; `__task_run` calls it for one chunk
// of a task. A state is reused for many tasks, so what a task can leave behind is closed: the libraries are read-only
// (the sandbox does that), the globals are put back when a task ends, `math.random` is seeded again for every task and
// every element, and the `random` table is built on it so the result depends only on the function, the arguments and
// the seed.
const char* LuaTaskHostSource()
{
    return R"LUA(
-- Everything the host uses is captured now, so a task that replaces a global cannot change what the host calls.
local pairs, pcall, error, tostring, setmetatable = pairs, pcall, error, tostring, setmetatable
local cache = {}
local count = 0
local seed_random = math.randomseed
local rnd = math.random

-- A definition is accepted only as the one call the engine's own chunk makes for the key it announced, and never while a
-- task runs, so a task's source cannot replace the function another task will run on this state.
local expected = nil
local running = false
local sealed = true  -- false from the announcement of a definition until the engine seals it after the load

function __task_expect(key)
  if running or not sealed then
    error('not available in a task', 2)
  end
  sealed = false
  expected = key
end

function __task_seal()
  if running then
    error('not available in a task', 2)
  end
  sealed = true
  expected = nil
end

function __task_define(key, mode, fn)
  if running or expected ~= key then
    error('unexpected task definition', 2)
  end
  expected = nil
  count = count + 1
  cache[key] = fn
end

function __task_forget()
  if running then
    error('not available in a task', 2)
  end
  cache = {}
  count = 0
end

local function run(key, mode, data, first, last, seed)
  local fn = cache[key]
  if fn == nil then
    error('the task function is not defined in this state', 0)
  end
  seed_random(seed)
  if mode == 1 then
    return fn(data)
  end
  if mode == 2 then
    local out = {}
    for j = 1, last - first + 1 do
      local index = first + j - 1
      seed_random(seed + index)
      local v = fn(data[j], index)
      if v == nil then
        error('task.map: the function returned nil for index ' .. index, 0)
      end
      out[j] = v
    end
    return out
  end
  local v = fn(first, last, data)
  if v == nil then
    error('task.parallel_for: the function returned nil for the chunk starting at ' .. first, 0)
  end
  return v
end

-- The binding streams of `random` follow the entity that asked; a task has none, so it gets one built on math.random.
local function make_random()
  return {
    float = function() return rnd() end,
    int = function(lo, hi) return rnd(lo, hi) end,
  }
end
random = make_random()

-- What the globals are now is what every task starts from: whatever a task adds, removes or replaces is put back when
-- it ends, so a task cannot leave anything for the next one (the sandbox keeps the globals table's metatable closed,
-- so this is done by restoring, not by a guard).
local base = {}
for k, v in pairs(_G) do
  base[k] = v
end

local function scrub()
  for k in pairs(_G) do
    if base[k] == nil then
      _G[k] = nil
    end
  end
  for k, v in pairs(base) do
    if _G[k] ~= v then
      _G[k] = v
    end
  end
end

function __task_run(...)
  if running then
    error('not available in a task', 2)
  end
  running = true  -- stays true until the scrub has finished
  local fresh = make_random()  -- a task may change the table it sees, never the next task's
  base.random = fresh
  _G.random = fresh
  local ok, result = pcall(run, ...)
  scrub()
  running = false
  if not ok then
    error(result, 0)
  end
  return result
end
base.__task_run = __task_run
)LUA";
}

u64 TaskValueBytes(const Value& value)
{
    u64 bytes = 16;
    switch (value.Kind())
    {
    case ValueKind::Text: bytes += value.Text().Length(); break;
    case ValueKind::Bytes: bytes += value.Bytes().Size(); break;
    case ValueKind::Array:
        for (usize i = 0; i < value.Size(); ++i)
        {
            bytes += TaskValueBytes(value.At(i));
        }
        break;
    case ValueKind::Map:
        for (usize i = 0; i < value.Size(); ++i)
        {
            bytes += value.KeyAt(i).Length() + TaskValueBytes(value.At(i));
        }
        break;
    default: break;
    }
    return bytes;
}

TakeStatus TakeTask(TaskPartition* view, u64 owner_key, u64 id, Value* out)
{
    for (usize i = 0; i < view->taken->Size(); ++i)
    {
        if ((*view->taken)[i] == id)
        {
            return TakeStatus::Unknown;
        }
    }
    const TaskRecord* record = view->table->Find(id);
    if (record == nullptr)
    {
        for (usize i = 0; i < view->outbox->requests.Size(); ++i)
        {
            if (view->outbox->requests[i].id == id)
            {
                return view->outbox->requests[i].owner_key == owner_key ? TakeStatus::Pending : TakeStatus::NotOwner;
            }
        }
        return TakeStatus::Unknown;
    }
    if (record->owner_key != owner_key)
    {
        return TakeStatus::NotOwner;
    }
    view->taken->Push(id);
    if (record->failed)
    {
        *out = Value::FromText(record->error.View());
        return TakeStatus::Failed;
    }
    *out = record->result;
    return TakeStatus::Ready;
}

TaskTable::TaskTable() : index_(HeapAllocator::Default()) {}

const TaskRecord* TaskTable::Find(u64 id) const
{
    const u32* at = index_.Find(id);
    return at != nullptr ? &slots_[*at].record : nullptr;
}

TaskRecord* TaskTable::Insert(u64 id)
{
    const u32* existing = index_.Find(id);
    u32 at = 0;
    if (existing != nullptr)
    {
        at = *existing;
        slots_[at].record = TaskRecord();
        return &slots_[at].record;
    }
    if (free_.Size() != 0)
    {
        at = free_[free_.Size() - 1];
        free_.Pop();
    }
    else
    {
        at = static_cast<u32>(slots_.Size());
        slots_.Push(Slot());
    }
    slots_[at].id = id;
    slots_[at].live = true;
    slots_[at].record = TaskRecord();
    index_.Insert(id, at);
    ++live_;
    return &slots_[at].record;
}

void TaskTable::Erase(u64 id)
{
    const u32* existing = index_.Find(id);
    if (existing == nullptr)
    {
        return;
    }
    const u32 at = *existing;
    slots_[at].live = false;
    slots_[at].record = TaskRecord();
    index_.Remove(id);
    free_.Push(at);
    --live_;
}

void TaskTable::EraseOwners(Span<const u64> owners, Vector<u32>* removed_per_partition)
{
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        Slot& s = slots_[i];
        if (!s.live)
        {
            continue;
        }
        usize lo = 0;
        usize hi = owners.Size();
        while (lo < hi)
        {
            const usize mid = lo + (hi - lo) / 2;
            if (owners[mid] < s.record.owner_key)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < owners.Size() && owners[lo] == s.record.owner_key)
        {
            const u32 partition = s.record.partition;
            while (removed_per_partition->Size() <= partition)
            {
                removed_per_partition->Push(0);
            }
            ++(*removed_per_partition)[partition];
            Erase(s.id);
        }
    }
}

namespace
{

constexpr usize kMaxKnownKeys = 256;
constexpr usize kMaxErrorBytes = 300;

// One piece of work: a whole `Run`, or a range of a `Map` or a `For`.
struct Item
{
    u32 request = 0;
    u64 first = 1;
    u64 last = 1;
    bool ran = false;
    bool failed = false;
    Value result;
    String<HeapAllocator> error{HeapAllocator::Default()};
    LogBuffer log{64};
};

u64 HashSource(TaskMode mode, StringView source, u64 basis = kFnv1a64Basis)
{
    const u64 h = Fnv1a64Builder(basis).AddWord(static_cast<u64>(mode)).AddText(source).Value();
    return h & 0xFFFFFFFFFFFFFull;  // 52 bits: a Lua number holds it exactly
}

void AppendNumber(String<HeapAllocator>* out, u64 n)
{
    char digits[24];
    usize len = 0;
    do
    {
        digits[len++] = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n != 0);
    while (len > 0)
    {
        const char c = digits[--len];
        out->Append(StringView(&c, 1));
    }
}

String<HeapAllocator> ShortError(StringView message)
{
    String<HeapAllocator> text(HeapAllocator::Default(), message.Length() > kMaxErrorBytes
                                                             ? StringView(message.Data(), kMaxErrorBytes)
                                                             : message);
    return text;
}

}  // namespace

struct TaskPool::Impl
{
    explicit Impl(const TaskPoolConfig& c) : config(c), known(), resets_seen() {}

    TaskPoolConfig config;
    UniquePtr<ScriptWorld> world;
    // Per state: the keys defined in it, each with a second, independent hash of its source (container audit Day 9: two
    // vectors in step, scanned on every task run).
    Vector<HashMap<u64, u64, HeapAllocator>> known;
    Vector<u64> resets_seen;
    Vector<String<HeapAllocator>> state_errors;  // why a state could not be checked out, per state

    void Ensure()
    {
        if (world.Get() != nullptr)
        {
            return;
        }
        ScriptWorldConfig wc;
        wc.backend = config.backend;
        wc.bindings = config.bindings;
        wc.profile = InstallProfile::Task();
        wc.partitions = config.states;
        wc.partition_memory = config.state_memory;
        world = MakeUnique<ScriptWorld>(wc);
        const char* host = config.host_source.Empty() ? LuaTaskHostSource() : nullptr;
        const StringView text = host != nullptr ? StringView(host) : config.host_source;
        world->AddScript("task_host", CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length())));
        for (u32 s = 0; s < config.states; ++s)
        {
            known.Push(HashMap<u64, u64, HeapAllocator>(HeapAllocator::Default()));
            resets_seen.Push(0);
            state_errors.Push(String<HeapAllocator>(HeapAllocator::Default()));
        }
    }

    // Makes sure `key` is defined in the state, then runs the chunk.
    void RunItem(PartitionLease* lease, u32 state, const TaskRequest& r, u64 key, Item* item)
    {
        item->ran = true;
        const u64 resets = world->Counters(state).resets;
        if (resets != resets_seen[state])
        {
            resets_seen[state] = resets;
            known[state].Clear();
        }
        HashMap<u64, u64, HeapAllocator>& keys = known[state];
        const u64 check = HashSource(r.mode, r.source.View(), 0x9E3779B97F4A7C15ull) |
                          1;  // a second, independent 52-bit hash
        bool defined = false;
        if (const u64* sum = keys.Find(key))
        {
            if (*sum != check)
            {  // two sources share a key: forget everything rather than run the wrong function
                CallOutput forget;
                lease->Call("__task_forget", Span<const Value>(), config.budget, &forget);
                keys.Clear();
            }
            else
            {
                defined = true;
            }
        }
        if (!defined)
        {
            if (keys.Size() >= kMaxKnownKeys)
            {
                CallOutput forget;
                lease->Call("__task_forget", Span<const Value>(), config.budget, &forget);
                keys.Clear();
            }
            Value announce[1] = {Value::FromInt(static_cast<i64>(key))};
            CallOutput expect;
            lease->Call("__task_expect", Span<const Value>(announce, 1), config.budget, &expect);
            String<HeapAllocator> text(HeapAllocator::Default(), "__task_define(");
            AppendNumber(&text, key);
            text.Append(StringView(", "));
            AppendNumber(&text, static_cast<u64>(r.mode));
            text.Append(StringView(r.mode == TaskMode::Run   ? ", function(args) "
                                   : r.mode == TaskMode::Map ? ", function(item, index) "
                                                             : ", function(first, last, args) "));
            text.Append(r.source.View());
            text.Append(StringView("\nend)"));
            const Outcome loaded = lease->GetState()->Load("task", text.View());
            CallOutput seal;
            lease->Call("__task_seal", Span<const Value>(), config.budget, &seal);
            if (!loaded.IsOk())
            {
                item->failed = true;
                item->error = ShortError(loaded.message.View());
                return;
            }
            keys.Insert(key, check);
        }
        Value data;
        if (r.mode == TaskMode::Map)
        {
            data = Value::MakeArray();
            for (u64 i = item->first; i <= item->last; ++i)
            {
                Value copy = r.data.At(static_cast<usize>(i - 1));
                data.Push(Move(copy));
            }
        }
        else
        {
            data = r.data;
        }
        Value args[6] = {Value::FromInt(static_cast<i64>(key)),
                         Value::FromInt(static_cast<i64>(r.mode)),
                         Move(data),
                         Value::FromInt(static_cast<i64>(item->first)),
                         Value::FromInt(static_cast<i64>(item->last)),
                         Value::FromInt(static_cast<i64>(r.seed & 0xFFFFFFFFull))};
        PartitionContext context;
        context.in_task = true;
        context.log = &item->log;
        context.random_state = r.seed | 1;
        ScopedPartitionContext scope(&context);
        CallOutput out;
        const Outcome outcome = lease->Call("__task_run", Span<const Value>(args, 6), config.budget, &out);
        if (!outcome.IsOk())
        {
            item->failed = true;
            item->error = ShortError(outcome.message.View());
            return;
        }
        if (out.results.Size() != 0)
        {
            item->result = Move(out.results[0]);
        }
    }
};

TaskPool::TaskPool(const TaskPoolConfig& config) : config_(config), impl_(MakeUnique<Impl>(config))
{
    ENGINE_ASSERT(config.backend != nullptr && config.bindings != nullptr, "TaskPool: needs a backend and bindings");
    ENGINE_ASSERT(config.states >= 1 && config.states <= kMaxPartitions,
                  "TaskPool: states must be 1 to kMaxPartitions");
}

TaskPool::~TaskPool() = default;

u64 TaskPool::Resets() const
{
    return impl_->world.Get() != nullptr ? impl_->world->Total().resets : 0;
}

usize TaskPool::Execute(Span<const TaskRequest> requests, Vector<TaskOutcome>* outcomes, jobs::Scheduler* scheduler)
{
    Impl& m = *impl_;
    outcomes->Clear();
    for (usize i = 0; i < requests.Size(); ++i)
    {
        outcomes->Push(TaskOutcome());
    }
    if (requests.Size() == 0)
    {
        return 0;
    }
    m.Ensure();
    for (usize i = 0; i < m.state_errors.Size(); ++i)
    {
        m.state_errors[i] = String<HeapAllocator>(HeapAllocator::Default());
    }
    Vector<UniquePtr<Item>> items;
    Vector<u64> keys;
    for (usize i = 0; i < requests.Size(); ++i)
    {
        const TaskRequest& r = requests[i];
        keys.Push(HashSource(r.mode, r.source.View()));
        TaskOutcome& o = (*outcomes)[i];
        u64 n = 1;
        if (r.mode == TaskMode::Map)
        {
            if (r.data.Kind() != ValueKind::Array)
            {
                o.failed = true;
                o.error = String<HeapAllocator>(HeapAllocator::Default(),
                                                "task.map: the second argument must be an array");
                continue;
            }
            n = r.data.Size();
        }
        else if (r.mode == TaskMode::For)
        {
            n = r.count;
        }
        if (r.mode == TaskMode::Run)
        {
            auto item = MakeUnique<Item>();
            item->request = static_cast<u32>(i);
            items.Push(Move(item));
            continue;
        }
        const u64 chunk = r.chunk != 0 ? r.chunk : kDefaultTaskChunk;
        const u64 chunks = n / chunk + (n % chunk != 0 ? 1 : 0);
        if (chunks > kMaxTaskChunks)
        {
            o.failed = true;
            o.error = String<HeapAllocator>(HeapAllocator::Default(),
                                            "too many chunks: give a larger chunk size (at most 65536 chunks)");
            continue;
        }
        for (u64 c = 0; c < chunks; ++c)
        {
            auto item = MakeUnique<Item>();
            item->request = static_cast<u32>(i);
            item->first = c * chunk + 1;
            item->last = (c + 1) * chunk < n ? (c + 1) * chunk : n;
            items.Push(Move(item));
        }
    }
    struct Shared
    {
        Impl* impl;
        Span<const TaskRequest> requests;
        Vector<UniquePtr<Item>>* items;
        Vector<u64>* keys;
        Atomic<usize> next{0};
    } shared{&m, requests, &items, &keys};
    auto run = [](void* user, usize begin, usize end)
    {
        Shared* s = static_cast<Shared*>(user);
        for (usize state = begin; state < end; ++state)
        {
            PartitionLease lease;
            Outcome error;
            if (s->impl->world->Checkout(static_cast<u32>(state), &lease, &error) != CheckoutStatus::Ok)
            {
                s->impl->state_errors[state] = ShortError(error.message.View());
                continue;  // the other states take its share; items nobody ran are reported below
            }
            for (;;)
            {
                const usize at = s->next.FetchAdd(1, MemoryOrder::Relaxed);
                if (at >= s->items->Size())
                {
                    break;
                }
                Item* item = (*s->items)[at].Get();
                s->impl->RunItem(&lease, static_cast<u32>(state), s->requests[item->request], (*s->keys)[item->request],
                                 item);
            }
        }
    };
    const usize jobs_count = items.Size() < m.config.states ? items.Size() : m.config.states;
    if (jobs_count != 0)
    {
        if (scheduler != nullptr && scheduler->ParticipantCount() > 1 && jobs_count > 1 &&
            !jobs::Scheduler::InsideJob() && jobs::Scheduler::Current() == scheduler)
        {
            jobs::ParallelForBlocking(*scheduler, jobs_count, 1, run, &shared);
        }
        else
        {
            run(&shared, 0, 1);  // one state does it all, in order
        }
    }
    Vector<LogBuffer*> logs;
    for (usize i = 0; i < items.Size(); ++i)
    {
        logs.Push(&items[i]->log);
    }
    FlushLogBuffers(Span<LogBuffer* const>(logs.Data(), logs.Size()));
    // Assemble in request and index order, so the answer does not depend on who ran what.
    usize cursor = 0;
    for (usize i = 0; i < requests.Size(); ++i)
    {
        const TaskRequest& r = requests[i];
        TaskOutcome& o = (*outcomes)[i];
        if (o.failed)
        {
            continue;
        }
        if (r.mode != TaskMode::Run)
        {
            o.value = Value::MakeArray();
        }
        while (cursor < items.Size() && items[cursor]->request == i)
        {
            Item& item = *items[cursor++];
            if (o.failed)
            {
                continue;
            }
            if (!item.ran)
            {
                o.failed = true;
                o.error = String<HeapAllocator>(HeapAllocator::Default(), "no task state was available");
                for (usize k = 0; k < m.state_errors.Size(); ++k)
                {
                    if (m.state_errors[k].Length() != 0)
                    {
                        o.error.Append(StringView(": "));
                        o.error.Append(m.state_errors[k].View());
                        break;
                    }
                }
            }
            else if (item.failed)
            {
                o.failed = true;
                if (r.mode == TaskMode::Run)
                {
                    o.error = Move(item.error);
                }
                else
                {
                    o.error = String<HeapAllocator>(HeapAllocator::Default(), "chunk starting at index ");
                    AppendNumber(&o.error, item.first);
                    o.error.Append(StringView(": "));
                    o.error.Append(item.error.View());
                }
            }
            else if (r.mode == TaskMode::Run)
            {
                o.value = Move(item.result);
            }
            else if (r.mode == TaskMode::For)
            {
                if (!o.value.Push(Move(item.result)))
                {
                    o.failed = true;
                    o.error = String<HeapAllocator>(HeapAllocator::Default(), "the results are too large");
                }
            }
            else
            {
                if (item.result.Kind() != ValueKind::Array)
                {
                    o.failed = true;
                    o.error = String<HeapAllocator>(HeapAllocator::Default(),
                                                    "task.map: a chunk did not return an array");
                    continue;
                }
                for (usize k = 0; k < item.result.Size(); ++k)
                {
                    Value copy = item.result.At(k);
                    if (!o.value.Push(Move(copy)))
                    {
                        o.failed = true;
                        o.error = String<HeapAllocator>(HeapAllocator::Default(), "the results are too large");
                        break;
                    }
                }
            }
        }
        if (o.failed)
        {
            o.value = Value();
        }
    }
    return items.Size();
}

}  // namespace vse::script

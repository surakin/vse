#include "script/script_debug.h"

#include "cstr/mem_ops.h"
#include "sync/mutex.h"

namespace engine_placeholder::script
{

namespace
{

enum StepMode : u32
{
    kStepNone = 0,
    kStepOver = 1,
    kStepInto = 2,
    kStepOut = 3,
};

Value MakeEvent(const char* name)
{
    Value event = Value::MakeMap();
    event.Set("event", Value::FromText(name));
    return event;
}

// The integer under `key`, or `fallback` when it is absent or not an integer.
i64 IntField(const Value& map, const char* key, i64 fallback)
{
    const Value* v = map.Find(key);
    return (v != nullptr && v->Kind() == ValueKind::Int) ? v->AsInt() : fallback;
}

}  // namespace

// ----------------------------------------------------------------------------------------------------------------
// InProcessDebugTransport

InProcessDebugTransport::InProcessDebugTransport(usize max_events)
{
    const usize n = max_events == 0 ? 1 : max_events;
    ring_.Resize(n);
}

void InProcessDebugTransport::SetCommandHandler(CommandHandler handler, void* user)
{
    LockGuard<Mutex> guard(handler_mutex_);  // waits for a command in flight
    handler_ = handler;
    user_ = user;
}

void InProcessDebugTransport::PostEvent(Value&& event)
{
    {
        LockGuard<Mutex> guard(mutex_);
        const usize capacity = ring_.Size();
        if (count_ == capacity)
        {
            head_ = (head_ + 1) % capacity;
            --count_;
            dropped_.FetchAdd(1, MemoryOrder::Relaxed);
        }
        ring_[(head_ + count_) % capacity] = Move(event);
        ++count_;
    }
    ready_.NotifyOne();
}

bool InProcessDebugTransport::Send(const Value& command)
{
    LockGuard<Mutex> guard(handler_mutex_);
    if (handler_ == nullptr)
    {
        return false;
    }
    handler_(user_, command);
    return true;
}

bool InProcessDebugTransport::NextEvent(Value* out, u32 timeout_ms)
{
    UniqueLock<Mutex> lock(mutex_);
    if (count_ == 0 && timeout_ms != 0)
    {
        ready_.WaitForMilliseconds(lock, timeout_ms, [this] { return count_ != 0; });
    }
    if (count_ == 0)
    {
        return false;
    }
    *out = Move(ring_[head_]);
    ring_[head_] = Value();
    head_ = (head_ + 1) % ring_.Size();
    --count_;
    return true;
}

// ----------------------------------------------------------------------------------------------------------------
// ScriptDebugger

// What the debugger keeps for one partition, and the session its state calls.
class PartitionDebug final : public DebugSession
{
public:
    PartitionDebug(ScriptDebugger* owner, u32 index) : owner_(owner), index_(index) {}

    bool WantsLines() override { return owner_->WantsLines(this); }
    bool AtLine(const SourceLocation& where, u32 depth) override { return owner_->AtLine(this, where, depth); }
    void Pause(const SourceLocation& where, u32 depth, DebugInspector* inspector) override
    {
        owner_->StopHere(this, where, depth, inspector);
    }

    ScriptDebugger* owner_;
    u32 index_;
    Atomic<u32> step_mode{kStepNone};
    Atomic<u32> step_depth{0};
    // Guarded by the debugger's mutex.
    bool stopped = false;
    bool resume = false;
    u32 stop_depth = 0;
    Vector<Value> requests;
    // Used only by the partition's own thread.
    u32 seen_version = 0xFFFFFFFFu;
    Vector<ScriptDebugger::Breakpoint> local;
    const char* reason = "breakpoint";
};

ScriptDebugger::ScriptDebugger(DebugTransport* transport, u32 partitions, bool pause_all)
    : transport_(transport), pause_all_(pause_all)
{
    for (u32 i = 0; i < partitions; ++i)
    {
        partitions_.Push(MakeUnique<PartitionDebug>(this, i));
    }
    transport_->SetCommandHandler([](void* user, const Value& command)
                                  { static_cast<ScriptDebugger*>(user)->Handle(command); }, this);
}

ScriptDebugger::~ScriptDebugger()
{
    transport_->SetCommandHandler(nullptr, nullptr);
    Detach();
    UniqueLock<Mutex> lock(mutex_);
    changed_.Wait(lock, [this] { return inside_ == 0; });
}

DebugSession* ScriptDebugger::SessionFor(u32 partition)
{
    return partition < partitions_.Size() ? partitions_[partition].Get() : nullptr;
}

void ScriptDebugger::Post(Value&& event)
{
    transport_->PostEvent(Move(event));
}

void ScriptDebugger::PostError(StringView message)
{
    Value event = MakeEvent("error");
    event.Set("message", Value::FromText(message));
    Post(Move(event));
}

bool ScriptDebugger::WantsLines(PartitionDebug* p)
{
    return breakpoint_count_.Load(MemoryOrder::Acquire) != 0 || pause_requested_.Load(MemoryOrder::Acquire) ||
           p->step_mode.Load(MemoryOrder::Acquire) != kStepNone;
}

bool ScriptDebugger::AtLine(PartitionDebug* p, const SourceLocation& where, u32 depth)
{
    const u32 mode = p->step_mode.Load(MemoryOrder::Acquire);
    if (mode != kStepNone)
    {
        const u32 base = p->step_depth.Load(MemoryOrder::Relaxed);
        const bool hit = mode == kStepInto || (mode == kStepOver && depth <= base) ||
                         (mode == kStepOut && depth < base);
        if (hit)
        {
            p->reason = "step";
            return true;
        }
    }
    if (pause_requested_.Load(MemoryOrder::Acquire))
    {
        bool expected = true;
        if (pause_requested_.CompareExchangeStrong(expected, false))
        {
            p->reason = "pause";
            return true;
        }
    }
    if (breakpoint_count_.Load(MemoryOrder::Acquire) != 0)
    {
        const u32 version = breakpoint_version_.Load(MemoryOrder::Acquire);
        if (version != p->seen_version)
        {
            LockGuard<Mutex> guard(mutex_);
            p->local.Clear();
            for (usize i = 0; i < breakpoints_.Size(); ++i)
            {
                Breakpoint copy;
                copy.script.Append(breakpoints_[i].script.View());
                copy.line = breakpoints_[i].line;
                p->local.Push(Move(copy));
            }
            p->seen_version = breakpoint_version_.Load(MemoryOrder::Relaxed);
        }
        const StringView chunk(where.chunk);
        for (usize i = 0; i < p->local.Size(); ++i)
        {
            const Breakpoint& b = p->local[i];
            if (b.line != where.line)
            {
                continue;
            }
            const StringView script = b.script.View();
            // A chunk name that was cut to fit names a prefix of the script's name.
            if (script == chunk || (where.truncated && script.StartsWith(chunk)))
            {
                p->reason = "breakpoint";
                return true;
            }
        }
    }
    return false;
}

void ScriptDebugger::Serve(PartitionDebug* p, const Value& request, DebugInspector* inspector)
{
    const Value* cmd = request.Find("cmd");
    const StringView name = cmd != nullptr ? cmd->Text() : StringView();
    if (name == StringView("stack"))
    {
        Value frames = Value::MakeArray();
        const usize count = inspector->FrameCount();
        for (usize level = 0; level < count; ++level)
        {
            DebugFrameInfo info;
            if (!inspector->Frame(level, &info))
            {
                break;
            }
            Value frame = Value::MakeMap();
            frame.Set("function", Value::FromText(info.function.View()));
            frame.Set("script", Value::FromText(StringView(info.where.chunk)));
            frame.Set("line", Value::FromInt(info.where.line));
            frames.Push(Move(frame));
        }
        Value event = MakeEvent("stack");
        event.Set("partition", Value::FromInt(p->index_));
        event.Set("frames", Move(frames));
        Post(Move(event));
    }
    else if (name == StringView("locals"))
    {
        const i64 level = IntField(request, "frame", 0);
        Vector<DebugVariable> variables;
        if (level < 0 || !inspector->Locals(static_cast<usize>(level), &variables))
        {
            PostError("locals: no such frame");
            return;
        }
        Value list = Value::MakeArray();
        for (usize i = 0; i < variables.Size(); ++i)
        {
            Value entry = Value::MakeMap();
            entry.Set("name", Value::FromText(variables[i].name.View()));
            entry.Set("value", Value(variables[i].value));
            list.Push(Move(entry));
        }
        Value event = MakeEvent("locals");
        event.Set("partition", Value::FromInt(p->index_));
        event.Set("frame", Value::FromInt(level));
        event.Set("variables", Move(list));
        Post(Move(event));
    }
}

void ScriptDebugger::StopHere(PartitionDebug* p, const SourceLocation& where, u32 depth, DebugInspector* inspector)
{
    {
        LockGuard<Mutex> guard(mutex_);
        p->stopped = true;
        p->resume = false;
        p->stop_depth = depth;
        p->step_mode.Store(kStepNone, MemoryOrder::Release);
        stopped_.Store(stopped_.Load(MemoryOrder::Relaxed) + 1, MemoryOrder::Release);
        ++inside_;
        stop_count_.FetchAdd(1, MemoryOrder::Relaxed);
    }
    Value event = MakeEvent("stopped");
    event.Set("partition", Value::FromInt(p->index_));
    event.Set("script", Value::FromText(StringView(where.chunk)));
    event.Set("line", Value::FromInt(where.line));
    event.Set("depth", Value::FromInt(depth));
    event.Set("reason", Value::FromText(p->reason));
    Post(Move(event));

    UniqueLock<Mutex> lock(mutex_);
    for (;;)
    {
        changed_.Wait(lock, [p] { return p->resume || !p->requests.Empty(); });
        if (!p->requests.Empty())
        {
            Value request = Move(p->requests[0]);
            p->requests.Remove(0);
            lock.Unlock();
            Serve(p, request, inspector);
            lock.Lock();
            continue;
        }
        break;
    }
    p->stopped = false;
    stopped_.Store(stopped_.Load(MemoryOrder::Relaxed) - 1, MemoryOrder::Release);
    lock.Unlock();
    Value done = MakeEvent("continued");
    done.Set("partition", Value::FromInt(p->index_));
    Post(Move(done));
    // The last use of the debugger: it may be destroyed as soon as `inside_` is 0, so the count drops, and the waiters
    // are told, under the lock.
    lock.Lock();
    --inside_;
    changed_.NotifyAll();
}

void ScriptDebugger::Gate()
{
    if (!pause_all_ || stopped_.Load(MemoryOrder::Acquire) == 0)
    {
        return;
    }
    UniqueLock<Mutex> lock(mutex_);
    ++inside_;
    changed_.Wait(lock, [this] { return stopped_.Load(MemoryOrder::Relaxed) == 0; });
    --inside_;
    changed_.NotifyAll();  // under the lock: the destructor waits for `inside_` to reach 0
}

PartitionDebug* ScriptDebugger::Target(const Value& command)
{
    // The caller holds the mutex.
    const i64 wanted = IntField(command, "partition", -1);
    if (wanted >= 0)
    {
        if (static_cast<usize>(wanted) < partitions_.Size() && partitions_[static_cast<usize>(wanted)]->stopped)
        {
            return partitions_[static_cast<usize>(wanted)].Get();
        }
        return nullptr;
    }
    for (usize i = 0; i < partitions_.Size(); ++i)
    {
        if (partitions_[i]->stopped)
        {
            return partitions_[i].Get();
        }
    }
    return nullptr;
}

void ScriptDebugger::Resume(const Value& command, u32 step_mode)
{
    bool found = false;
    {
        LockGuard<Mutex> guard(mutex_);
        PartitionDebug* p = Target(command);
        if (p != nullptr)
        {
            found = true;
            p->step_depth.Store(p->stop_depth, MemoryOrder::Relaxed);
            p->step_mode.Store(step_mode, MemoryOrder::Release);
            p->resume = true;
        }
    }
    if (found)
    {
        changed_.NotifyAll();
    }
    else
    {
        PostError("no stopped partition");
    }
}

void ScriptDebugger::Request(const Value& command)
{
    bool found = false;
    {
        LockGuard<Mutex> guard(mutex_);
        PartitionDebug* p = Target(command);
        if (p != nullptr)
        {
            found = true;
            p->requests.Push(Value(command));
        }
    }
    if (found)
    {
        changed_.NotifyAll();
    }
    else
    {
        PostError("no stopped partition");
    }
}

void ScriptDebugger::SetBreakpoints(const Value& command)
{
    const Value* list = command.Find("breakpoints");
    if (list == nullptr || list->Kind() != ValueKind::Array)
    {
        PostError("set_breakpoints: 'breakpoints' must be an array of {script, line}");
        return;
    }
    Vector<Breakpoint> fresh;
    for (usize i = 0; i < list->Size(); ++i)
    {
        const Value& entry = list->At(i);
        const Value* script = entry.Kind() == ValueKind::Map ? entry.Find("script") : nullptr;
        const i64 line = entry.Kind() == ValueKind::Map ? IntField(entry, "line", 0) : 0;
        if (script == nullptr || script->Kind() != ValueKind::Text || line <= 0)
        {
            PostError("set_breakpoints: each entry needs a 'script' name and a positive 'line'");
            return;
        }
        Breakpoint b;
        b.script.Append(script->Text());
        b.line = static_cast<u32>(line);
        fresh.Push(Move(b));
    }
    const u32 count = static_cast<u32>(fresh.Size());
    {
        LockGuard<Mutex> guard(mutex_);
        breakpoints_ = Move(fresh);
        breakpoint_version_.Store(breakpoint_version_.Load(MemoryOrder::Relaxed) + 1, MemoryOrder::Release);
        breakpoint_count_.Store(count, MemoryOrder::Release);
    }
    Value event = MakeEvent("breakpoints");
    event.Set("count", Value::FromInt(count));
    Post(Move(event));
}

void ScriptDebugger::Handle(const Value& command)
{
    const Value* cmd = command.Kind() == ValueKind::Map ? command.Find("cmd") : nullptr;
    if (cmd == nullptr || cmd->Kind() != ValueKind::Text)
    {
        PostError("a command is a map with a 'cmd' text");
        return;
    }
    const StringView name = cmd->Text();
    if (name == StringView("set_breakpoints"))
    {
        SetBreakpoints(command);
    }
    else if (name == StringView("pause"))
    {
        pause_requested_.Store(true, MemoryOrder::Release);
    }
    else if (name == StringView("continue"))
    {
        Resume(command, kStepNone);
    }
    else if (name == StringView("step_over"))
    {
        Resume(command, kStepOver);
    }
    else if (name == StringView("step_in"))
    {
        Resume(command, kStepInto);
    }
    else if (name == StringView("step_out"))
    {
        Resume(command, kStepOut);
    }
    else if (name == StringView("stack") || name == StringView("locals"))
    {
        Request(command);
    }
    else if (name == StringView("detach"))
    {
        Detach();
    }
    else
    {
        PostError("unknown command");
    }
}

void ScriptDebugger::Detach()
{
    {
        LockGuard<Mutex> guard(mutex_);
        breakpoints_.Clear();
        breakpoint_version_.Store(breakpoint_version_.Load(MemoryOrder::Relaxed) + 1, MemoryOrder::Release);
        breakpoint_count_.Store(0, MemoryOrder::Release);
        pause_requested_.Store(false, MemoryOrder::Release);
        for (usize i = 0; i < partitions_.Size(); ++i)
        {
            PartitionDebug* p = partitions_[i].Get();
            p->step_mode.Store(kStepNone, MemoryOrder::Release);
            p->requests.Clear();
            if (p->stopped)
            {
                p->resume = true;
            }
        }
    }
    changed_.NotifyAll();
}

}  // namespace engine_placeholder::script

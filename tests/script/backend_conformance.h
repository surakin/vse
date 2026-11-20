// The backend conformance suite (Phase 27 Day 447): one set of behavioral cases every language backend must pass, so
// "the engine does not depend on Lua" is a test and not a claim. A backend's test file builds a `ConformanceKit`
// (the same functions written in its language) and calls `RunConformance`; the result lists the cases that failed and
// the cases that were skipped with the reason (a capability the backend does not advertise).
//
// The cases collect into a report instead of using Catch macros so the planted-fault test can assert that a wrong
// backend fails the right case.
#pragma once

#include "script/backend.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::script::test
{

/// The language-specific half of the suite: sources that define the functions the cases call.
struct ConformanceKit
{
    /// A chunk that defines, as globals: `echo(a)` returns `a`; `multi()` returns 1, 2.5, "x"; `fail()` raises a
    /// runtime error; `calls_native()` returns the result of the native `host_add(2, 3)`; `waiter()` waits 0.25
    /// seconds and returns the value it is resumed with; `setg(v)` stores `v` in a global; `getg()` returns it;
    /// `spin()` loops forever; `bomb()` allocates without end (only called when the backend advertises a memory
    /// budget).
    const char* prelude = "";
    /// A source that does not compile.
    const char* syntax_error = "";
    /// Sources a hostile author might write; loading each must not crash, hang or leak (any status is fine).
    Vector<const char*> hostile;
    /// Whether the language keeps 64-bit integers exactly (false for a language whose numbers are doubles: the case
    /// then uses +-2^53).
    bool exact_int64 = true;
};

/// The outcome of a run: names of failed cases and "name: reason" lines for skipped ones.
struct ConformanceReport
{
    Vector<String<HeapAllocator>> failed;
    Vector<String<HeapAllocator>> skipped;
    usize passed = 0;

    bool Failed(const char* name) const
    {
        for (usize i = 0; i < failed.Size(); ++i)
        {
            if (failed[i].View() == StringView(name))
            {
                return true;
            }
        }
        return false;
    }
};

namespace conformance_detail
{

inline Outcome HostAdd(void*, Span<const Value> args, Vector<Value>* results)
{
    if (args.Size() != 2 || !args[0].IsNumber() || !args[1].IsNumber())
    {
        return Outcome(Status::RuntimeError, "host_add wants two numbers");
    }
    results->Push(Value::FromInt(static_cast<i64>(args[0].ToDouble() + args[1].ToDouble())));
    return Outcome();
}

class Recorder
{
public:
    explicit Recorder(ConformanceReport* report, const char* name) : report_(report), name_(name) {}
    ~Recorder()
    {
        if (ok_)
        {
            ++report_->passed;
        }
        else
        {
            report_->failed.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(name_)));
        }
    }
    void Expect(bool condition)
    {
        if (!condition)
        {
            ok_ = false;
        }
    }

private:
    ConformanceReport* report_;
    const char* name_;
    bool ok_ = true;
};

inline void Skip(ConformanceReport* report, const char* name, const char* reason)
{
    String<HeapAllocator> line(HeapAllocator::Default(), StringView(name));
    line.Append(": ");
    line.Append(StringView(reason));
    report->skipped.Push(Move(line));
}

inline UniquePtr<State> Prepared(Backend& backend, const ConformanceKit& kit, bool* ok)
{
    UniquePtr<State> state = backend.CreateState(0);
    if (state.Get() == nullptr)
    {
        *ok = false;
        return state;
    }
    if (!state->RegisterFunction("host_add", &HostAdd, nullptr).IsOk() || !state->Load("prelude", kit.prelude).IsOk())
    {
        *ok = false;
    }
    return state;
}

inline bool Call(State& state, const char* fn, Span<const Value> args, const Budget& budget, CallOutput* out,
                 Status* status)
{
    const Outcome o = state.Call(fn, args, budget, out);
    *status = o.status;
    return o.IsOk();
}

struct ThreadJob
{
    Backend* backend = nullptr;
    const ConformanceKit* kit = nullptr;
    int index = 0;
    Atomic<u32>* failures = nullptr;

    void operator()() const
    {
        bool ok = true;
        UniquePtr<State> state = Prepared(*backend, *kit, &ok);
        if (!ok)
        {
            failures->FetchAdd(1, MemoryOrder::Relaxed);
            return;
        }
        for (int i = 0; i < 200; ++i)
        {
            CallOutput out;
            Value arg = Value::FromInt(index * 1000 + i);
            Value set_args[1] = {arg};
            if (!state->Call("setg", Span<const Value>(set_args, 1), Budget(), &out).IsOk() ||
                !state->Call("getg", Span<const Value>(), Budget(), &out).IsOk() || out.results.Size() != 1 ||
                !out.results[0].Equals(arg))
            {
                failures->FetchAdd(1, MemoryOrder::Relaxed);
                return;
            }
        }
    }
};

}  // namespace conformance_detail

/// Runs every case against `backend` with `kit`.
inline ConformanceReport RunConformance(Backend& backend, const ConformanceKit& kit)
{
    using namespace conformance_detail;
    ConformanceReport report;
    const Capabilities caps = backend.Caps();
    const Budget none;

    {
        Recorder r(&report, "load_and_call");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Value args[1] = {Value::FromInt(42)};
            CallOutput out;
            r.Expect(state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk());
            r.Expect(out.results.Size() == 1 && out.results[0].Equals(Value::FromInt(42)));
            r.Expect(!out.Suspended());
        }
    }
    {
        Recorder r(&report, "marshal_every_kind");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Value array = Value::MakeArray();
            array.Push(Value::FromInt(1));
            array.Push(Value::FromText("two"));
            Value map = Value::MakeMap();
            map.Set("k", Value::FromFloat(0.5));
            map.Set("nested", Value::FromBool(true));
            const u8 raw[3] = {1, 2, 255};
            Vector<Value> samples;
            samples.Push(Value());
            samples.Push(Value::FromBool(false));
            samples.Push(Value::FromBool(true));
            samples.Push(Value::FromInt(kit.exact_int64 ? -9007199254740993LL : -9007199254740992LL));
            samples.Push(Value::FromFloat(-2.5));
            samples.Push(Value::FromText(""));
            samples.Push(Value::FromText("héllo\n"));
            samples.Push(Move(array));
            samples.Push(Move(map));
            samples.Push(Value::FromBytes(Span<const u8>(raw, 3)));
            for (usize i = 0; i < samples.Size(); ++i)
            {
                Value args[1] = {samples[i]};
                CallOutput out;
                const bool called = state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk();
                r.Expect(called && out.results.Size() == 1 && out.results[0].Equals(samples[i]));
            }
        }
    }
    {
        Recorder r(&report, "multiple_results");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            CallOutput out;
            r.Expect(state->Call("multi", Span<const Value>(), none, &out).IsOk());
            r.Expect(out.results.Size() == 3 && out.results[0].Equals(Value::FromInt(1)) &&
                     out.results[1].Equals(Value::FromFloat(2.5)) && out.results[2].Equals(Value::FromText("x")));
        }
    }
    {
        Recorder r(&report, "errors_are_values");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            CallOutput out;
            const Outcome failed = state->Call("fail", Span<const Value>(), none, &out);
            r.Expect(failed.status == Status::RuntimeError && failed.message.Length() > 0);
            r.Expect(out.results.Size() == 0);
            r.Expect(state->Call("no_such_function", Span<const Value>(), none, &out).status == Status::RuntimeError);
            r.Expect(state->Load("broken", kit.syntax_error).status == Status::SyntaxError);
            r.Expect(state->Load("", "x").status == Status::Invalid);
            r.Expect(state->Call("", Span<const Value>(), none, &out).status == Status::Invalid);
            Value args[1] = {Value::FromInt(7)};
            r.Expect(state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk() && out.results.Size() == 1);
        }
    }
    {
        Recorder r(&report, "native_functions");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            CallOutput out;
            r.Expect(state->Call("calls_native", Span<const Value>(), none, &out).IsOk());
            r.Expect(out.results.Size() == 1 && out.results[0].Equals(Value::FromInt(5)));
            r.Expect(!state->RegisterFunction("", &HostAdd, nullptr).IsOk());
            r.Expect(!state->RegisterFunction("x", nullptr, nullptr).IsOk());
        }
    }
    if (caps.instruction_budget)
    {
        Recorder r(&report, "instruction_budget");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Budget budget;
            budget.instructions = 100000;
            CallOutput out;
            r.Expect(state->Call("spin", Span<const Value>(), budget, &out).status == Status::BudgetExceeded);
            Value args[1] = {Value::FromInt(1)};
            r.Expect(state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk());
        }
    }
    else
    {
        Skip(&report, "instruction_budget", "backend does not advertise instruction budgets");
    }
    if (caps.time_budget)
    {
        Recorder r(&report, "time_budget");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Budget budget;
            budget.nanoseconds = 20'000'000;
            CallOutput out;
            r.Expect(state->Call("spin", Span<const Value>(), budget, &out).status == Status::BudgetExceeded);
        }
    }
    else
    {
        Skip(&report, "time_budget", "backend does not advertise time budgets");
    }
    if (caps.memory_budget)
    {
        Recorder r(&report, "memory_budget");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Budget budget;
            budget.memory_bytes = 4 << 20;
            CallOutput out;
            r.Expect(state->Call("bomb", Span<const Value>(), budget, &out).status == Status::OutOfMemory);
            state->Collect();
            Value args[1] = {Value::FromInt(1)};
            r.Expect(state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk());
        }
    }
    else
    {
        Recorder r(&report, "unhonored_budget_is_unsupported");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            Budget budget;
            budget.memory_bytes = 1 << 20;
            CallOutput out;
            Value args[1] = {Value::FromInt(1)};
            r.Expect(state->Call("echo", Span<const Value>(args, 1), budget, &out).status == Status::Unsupported);
        }
        Skip(&report, "memory_budget", "backend does not advertise memory budgets");
    }
    if (caps.coroutines)
    {
        Recorder r(&report, "coroutines");
        bool ok = true;
        UniquePtr<State> state = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            CallOutput out;
            r.Expect(state->Call("waiter", Span<const Value>(), none, &out).IsOk());
            r.Expect(out.Suspended() && out.coroutine != 0 && out.wait.kind == WaitRequest::Kind::Seconds &&
                     out.wait.seconds == 0.25);
            const u64 id = out.coroutine;
            Value resume[1] = {Value::FromInt(9)};
            CallOutput done;
            r.Expect(state->Resume(id, Span<const Value>(resume, 1), none, &done).IsOk());
            r.Expect(!done.Suspended() && done.results.Size() == 1 && done.results[0].Equals(Value::FromInt(9)));
            r.Expect(state->Resume(id, Span<const Value>(resume, 1), none, &done).status == Status::Invalid);
            CallOutput second;
            r.Expect(state->Call("waiter", Span<const Value>(), none, &second).IsOk() && second.Suspended());
            state->Abandon(second.coroutine);
            r.Expect(state->Resume(second.coroutine, Span<const Value>(), none, &done).status == Status::Invalid);
            state->Abandon(123456);
        }
    }
    else
    {
        Skip(&report, "coroutines", "backend does not advertise coroutines");
    }
    {
        Recorder r(&report, "state_independence");
        bool ok = true;
        UniquePtr<State> a = Prepared(backend, kit, &ok);
        UniquePtr<State> b = Prepared(backend, kit, &ok);
        r.Expect(ok);
        if (ok)
        {
            CallOutput out;
            Value args[1] = {Value::FromInt(77)};
            r.Expect(a->Call("setg", Span<const Value>(args, 1), none, &out).IsOk());
            r.Expect(b->Call("getg", Span<const Value>(), none, &out).IsOk());
            r.Expect(out.results.Size() == 0 || out.results[0].IsNil());
            r.Expect(a->Call("getg", Span<const Value>(), none, &out).IsOk() && out.results.Size() == 1 &&
                     out.results[0].Equals(Value::FromInt(77)));
        }
    }
    {
        Recorder r(&report, "many_states_many_threads");
        Atomic<u32> failures(0);
        Vector<Thread> threads;
        for (int i = 0; i < 8; ++i)
        {
            ThreadJob job;
            job.backend = &backend;
            job.kit = &kit;
            job.index = i;
            job.failures = &failures;
            threads.Push(Thread(job));
        }
        for (usize i = 0; i < threads.Size(); ++i)
        {
            threads[i].Join();
        }
        r.Expect(failures.Load(MemoryOrder::Relaxed) == 0);
    }
    {
        Recorder r(&report, "hostile_sources");
        for (usize i = 0; i < kit.hostile.Size(); ++i)
        {
            bool ok = true;
            UniquePtr<State> state = Prepared(backend, kit, &ok);
            r.Expect(ok);
            if (!ok)
            {
                break;
            }
            const Outcome o = state->Load("hostile", kit.hostile[i]);
            r.Expect(o.status != Status::Invalid);  // any other status is acceptable; reaching here means no crash
            Value args[1] = {Value::FromInt(1)};
            CallOutput out;
            r.Expect(state->Call("echo", Span<const Value>(args, 1), none, &out).IsOk());  // the state survives
        }
    }
    return report;
}

}  // namespace vse::script::test

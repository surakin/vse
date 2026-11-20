// A scripted fake backend (Phase 27 Day 446): behaves according to the function name it is asked to call, so the tests
// of the interface (and, from Day 447, the planted-fault checks of the conformance suite) need no language. The
// functions it "has":
//   echo          returns a copy of its arguments
//   fail          RuntimeError with a message and a trace
//   oom           OutOfMemory
//   spin          BudgetExceeded when the budget has an instruction or time limit, else Ok
//   wait          suspends with a 0.5 second wait (Unsupported when the fake is built without coroutines)
//   native.<name> calls the native function registered as <name>
// Load fails with SyntaxError for the source "syntax error" and records every chunk name.
#pragma once

#include "script/backend.h"

namespace vse::script::test
{

struct FakeNative
{
    FakeNative() : name(HeapAllocator::Default()) {}
    String<HeapAllocator> name;
    NativeFunction function = nullptr;
    void* user = nullptr;
};

class FakeState : public State
{
public:
    explicit FakeState(Capabilities caps) : caps_(caps) {}

    Vector<String<HeapAllocator>> chunks;
    usize collects = 0;
    bool closed = false;
    u64 next_coroutine = 1;
    u64 suspended = 0;

protected:
    Outcome DoLoad(StringView chunk_name, StringView source) override
    {
        if (chunk_name.Length() == 0)
        {
            return Outcome(Status::Invalid, "empty chunk name");
        }
        if (source == StringView("syntax error"))
        {
            Outcome out(Status::SyntaxError, "unexpected symbol near 'error'");
            out.trace = String<HeapAllocator>(HeapAllocator::Default(), "chunk:1");
            return out;
        }
        chunks.Push(String<HeapAllocator>(HeapAllocator::Default(), chunk_name));
        return Outcome();
    }

    Outcome DoCall(StringView path, Span<const Value> args, const Budget& budget, CallOutput* out) override
    {
        if (path.Length() == 0)
        {
            return Outcome(Status::Invalid, "empty function path");
        }
        if ((budget.instructions != 0 && !caps_.instruction_budget) ||
            (budget.nanoseconds != 0 && !caps_.time_budget) || (budget.memory_bytes != 0 && !caps_.memory_budget))
        {
            return Outcome(Status::Unsupported, "the backend cannot honor this budget");
        }
        for (usize i = 0; i < args.Size(); ++i)
        {
            if (args[i].Check(ValueLimits()) != ValueCheck::Ok)
            {
                return Outcome(Status::Invalid, "an argument is over the value limits");
            }
        }
        if (path == StringView("echo"))
        {
            for (usize i = 0; i < args.Size(); ++i)
            {
                out->results.Push(Value(args[i]));
            }
            return Outcome();
        }
        if (path == StringView("fail"))
        {
            Outcome fail(Status::RuntimeError, "boom");
            fail.trace = String<HeapAllocator>(HeapAllocator::Default(), "fail:3\nmain:1");
            return fail;
        }
        if (path == StringView("oom"))
        {
            return Outcome(Status::OutOfMemory, "not enough memory");
        }
        if (path == StringView("spin"))
        {
            if (budget.instructions != 0 || budget.nanoseconds != 0)
            {
                return Outcome(Status::BudgetExceeded, "budget exceeded");
            }
            return Outcome();
        }
        if (path == StringView("wait"))
        {
            if (!caps_.coroutines)
            {
                return Outcome(Status::Unsupported, "no coroutines");
            }
            out->wait.kind = WaitRequest::Kind::Seconds;
            out->wait.seconds = 0.5;
            out->coroutine = next_coroutine++;
            suspended = out->coroutine;
            return Outcome();
        }
        if (path.Length() > 7 && StringView(path.Data(), 7) == StringView("native."))
        {
            const StringView name(path.Data() + 7, path.Length() - 7);
            for (usize i = 0; i < natives_.Size(); ++i)
            {
                if (natives_[i].name.View() == name)
                {
                    return natives_[i].function(natives_[i].user, args, &out->results);
                }
            }
        }
        return Outcome(Status::RuntimeError, "attempt to call a nil value");
    }

    Outcome DoResume(u64 coroutine, Span<const Value> values, const Budget&, CallOutput* out) override
    {
        if (!caps_.coroutines)
        {
            return Outcome(Status::Unsupported, "no coroutines");
        }
        if (coroutine == 0 || coroutine != suspended)
        {
            return Outcome(Status::Invalid, "not suspended");
        }
        suspended = 0;
        for (usize i = 0; i < values.Size(); ++i)
        {
            out->results.Push(Value(values[i]));
        }
        return Outcome();
    }

    void DoAbandon(u64 coroutine) override
    {
        if (coroutine == suspended)
        {
            suspended = 0;
        }
    }

    Outcome DoRegisterFunction(StringView name, NativeFunction function, void* user) override
    {
        if (name.Length() == 0 || function == nullptr)
        {
            return Outcome(Status::Invalid, "a native function needs a name and a function");
        }
        FakeNative native;
        native.name = String<HeapAllocator>(HeapAllocator::Default(), name);
        native.function = function;
        native.user = user;
        natives_.Push(Move(native));
        return Outcome();
    }

    void DoCollect() override { ++collects; }
    usize DoMemoryUsed() override { return 1000 + 100 * chunks.Size(); }
    void DoClose() override { closed = true; }
    Capabilities DoCapabilities() const override { return caps_; }

private:
    Capabilities caps_;
    Vector<FakeNative> natives_;
};

class FakeBackend : public Backend
{
public:
    explicit FakeBackend(Capabilities caps) : caps_(caps) {}

    StringView Name() const override { return StringView("fake"); }
    StringView Version() const override { return StringView("0"); }
    Capabilities Caps() const override { return caps_; }
    UniquePtr<State> CreateState(u64) override { return MakeUnique<FakeState>(caps_); }

private:
    Capabilities caps_;
};

inline Capabilities FullCaps()
{
    Capabilities caps;
    caps.coroutines = true;
    caps.instruction_budget = true;
    caps.time_budget = true;
    caps.memory_budget = true;
    return caps;
}

}  // namespace vse::script::test

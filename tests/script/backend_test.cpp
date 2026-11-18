// The backend interface (Phase 27 Day 446), against the scripted fake backend: every status path, the value limits at
// the call boundary, native functions, suspension and resumption, capability reporting, and the owner rule.
#include <catch2/catch_test_macros.hpp>

#include "fake_backend.h"
#include "log/assert.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;
using namespace engine_placeholder::script::test;

namespace
{

UniquePtr<State> NewState(const Capabilities& caps)
{
    FakeBackend backend(caps);
    return backend.CreateState(0);
}

Outcome Double(void*, Span<const Value> args, Vector<Value>* results)
{
    if (args.Size() != 1 || !args[0].IsNumber())
    {
        return Outcome(Status::RuntimeError, "double wants one number");
    }
    results->Push(Value::FromFloat(args[0].ToDouble() * 2.0));
    return Outcome();
}

}  // namespace

TEST_CASE("Backend: reports its name, version and capabilities", "[script][backend]")
{
    FakeBackend backend(FullCaps());
    CHECK(backend.Name() == StringView("fake"));
    CHECK(backend.Caps().coroutines);
    CHECK_FALSE(backend.Caps().jit);
    UniquePtr<State> state = backend.CreateState(0);
    REQUIRE(state.Get() != nullptr);
    CHECK(state->Caps().instruction_budget);
}

TEST_CASE("State: Load reports syntax errors and bad names as values", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CHECK(state->Load("main", "x = 1").IsOk());
    const Outcome syntax = state->Load("broken", "syntax error");
    CHECK(syntax.status == Status::SyntaxError);
    CHECK(syntax.message.Length() > 0);
    CHECK(syntax.trace.View() == StringView("chunk:1"));
    CHECK(state->Load("", "x = 1").status == Status::Invalid);
}

TEST_CASE("State: Call returns copies of the arguments", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    Value args[2] = {Value::FromInt(3), Value::FromText("hi")};
    CallOutput out;
    REQUIRE(state->Call("echo", Span<const Value>(args, 2), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 2);
    CHECK(out.results[0].Equals(args[0]));
    CHECK(out.results[1].Text() == StringView("hi"));
    CHECK_FALSE(out.Suspended());
}

TEST_CASE("State: Call and Resume empty a reused output first", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CallOutput out;
    REQUIRE(state->Call("wait", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.Suspended());
    Value arg = Value::FromInt(1);
    REQUIRE(state->Call("echo", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
    CHECK_FALSE(out.Suspended());
    CHECK(out.coroutine == 0);
    CHECK(out.results.Size() == 1);
    REQUIRE(state->Call("echo", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
    CHECK(out.results.Size() == 1);  // not 2: the results of the earlier call are gone
    CHECK(state->Call("echo", Span<const Value>(), Budget(), nullptr).status == Status::Invalid);
    CHECK(state->Resume(1, Span<const Value>(), Budget(), nullptr).status == Status::Invalid);
}

TEST_CASE("State: every failure status comes back with a message", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CallOutput out;
    const Outcome fail = state->Call("fail", Span<const Value>(), Budget(), &out);
    CHECK(fail.status == Status::RuntimeError);
    CHECK(fail.message.View() == StringView("boom"));
    CHECK(fail.trace.Length() > 0);
    CHECK(state->Call("oom", Span<const Value>(), Budget(), &out).status == Status::OutOfMemory);
    CHECK(state->Call("missing", Span<const Value>(), Budget(), &out).status == Status::RuntimeError);
    CHECK(state->Call("", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    Budget limited;
    limited.instructions = 1000;
    CHECK(state->Call("spin", Span<const Value>(), limited, &out).status == Status::BudgetExceeded);
    CHECK(state->Call("spin", Span<const Value>(), Budget(), &out).IsOk());
    for (const Status s : {Status::Ok, Status::SyntaxError, Status::RuntimeError, Status::BudgetExceeded,
                           Status::OutOfMemory, Status::Invalid, Status::Unsupported})
    {
        CHECK(CStrLength(StatusName(s)) > 0);
    }
}

TEST_CASE("State: a value over the limits is refused at the call", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    const Vector<u8> big = []
    {
        Vector<u8> v;
        v.Resize(static_cast<usize>(ValueLimits().max_blob_bytes) + 1);
        return v;
    }();
    Value arg = Value::FromBytes(Span<const u8>(big.Data(), big.Size()));
    CallOutput out;
    CHECK(state->Call("echo", Span<const Value>(&arg, 1), Budget(), &out).status == Status::Invalid);
}

TEST_CASE("State: a budget the backend cannot honor is Unsupported, not ignored", "[script][backend]")
{
    Capabilities caps;  // honors no budget
    UniquePtr<State> state = NewState(caps);
    CallOutput out;
    Budget budget;
    budget.instructions = 100;
    CHECK(state->Call("echo", Span<const Value>(), budget, &out).status == Status::Unsupported);
    budget = Budget();
    budget.nanoseconds = 100;
    CHECK(state->Call("echo", Span<const Value>(), budget, &out).status == Status::Unsupported);
    budget = Budget();
    budget.memory_bytes = 100;
    CHECK(state->Call("echo", Span<const Value>(), budget, &out).status == Status::Unsupported);
    CHECK(state->Call("echo", Span<const Value>(), Budget(), &out).IsOk());
}

TEST_CASE("State: registered natives are called with copies and report errors", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CHECK(state->RegisterFunction("double", &Double, nullptr).IsOk());
    CHECK(state->RegisterFunction("", &Double, nullptr).status == Status::Invalid);
    CHECK(state->RegisterFunction("none", nullptr, nullptr).status == Status::Invalid);
    Value arg = Value::FromInt(21);
    CallOutput out;
    REQUIRE(state->Call("native.double", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].AsFloat() == 42.0);
    CallOutput bad;
    CHECK(state->Call("native.double", Span<const Value>(), Budget(), &bad).status == Status::RuntimeError);
}

TEST_CASE("State: a call suspends with a wait request and resumes with values", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CallOutput out;
    REQUIRE(state->Call("wait", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.Suspended());
    CHECK(out.wait.kind == WaitRequest::Kind::Seconds);
    CHECK(out.wait.seconds == 0.5);
    CHECK(out.coroutine != 0);

    Value v = Value::FromText("woke");
    CallOutput after;
    REQUIRE(state->Resume(out.coroutine, Span<const Value>(&v, 1), Budget(), &after).IsOk());
    REQUIRE(after.results.Size() == 1);
    CHECK(after.results[0].Text() == StringView("woke"));
    CHECK(state->Resume(out.coroutine, Span<const Value>(), Budget(), &after).status ==
          Status::Invalid);  // already resumed

    CallOutput second;
    REQUIRE(state->Call("wait", Span<const Value>(), Budget(), &second).IsOk());
    state->Abandon(second.coroutine);
    CHECK(state->Resume(second.coroutine, Span<const Value>(), Budget(), &after).status == Status::Invalid);
    state->Abandon(12345);  // an id that is not suspended is ignored
}

TEST_CASE("State: a backend without coroutines says so", "[script][backend]")
{
    Capabilities caps = FullCaps();
    caps.coroutines = false;
    UniquePtr<State> state = NewState(caps);
    CallOutput out;
    CHECK(state->Call("wait", Span<const Value>(), Budget(), &out).status == Status::Unsupported);
    CHECK(state->Resume(1, Span<const Value>(), Budget(), &out).status == Status::Unsupported);
}

TEST_CASE("State: Collect, MemoryUsed and Close reach the backend", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    FakeState* fake = static_cast<FakeState*>(state.Get());
    state->Load("a", "x");
    state->Load("b", "y");
    CHECK(state->MemoryUsed() == 1200);
    state->Collect();
    CHECK(fake->collects == 1);
    state->Close();
    CHECK(fake->closed);
}

TEST_CASE("State: ownership is handed over, never shared", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    CHECK(state->OwnedByCaller());
    state->HandOver();
    CHECK_FALSE(state->OwnedByCaller());

    // Another thread takes it, uses it, and hands it back; the values it returns are copies the first thread can read.
    Value result;
    Thread worker(
        [&]
        {
            state->Acquire();
            Value arg = Value::FromText("from the worker");
            CallOutput out;
            REQUIRE(state->Call("echo", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
            result = Move(out.results[0]);
            state->HandOver();
        });
    worker.Join();
    CHECK(result.Text() == StringView("from the worker"));
    state->Acquire();
    CHECK(state->OwnedByCaller());
    CallOutput out;
    CHECK(state->Call("echo", Span<const Value>(), Budget(), &out).IsOk());
}

#if ENGINE_DEBUG_CHECKS
namespace
{

i64 g_asserts = 0;

AssertAction CountAssert(const AssertInfo&)
{
    ++g_asserts;
    return AssertAction::Continue;
}

}  // namespace

TEST_CASE("State: a call from a thread that does not own the state is reported", "[script][backend]")
{
    UniquePtr<State> state = NewState(FullCaps());
    const AssertHandler previous = SetAssertHandler(&CountAssert);
    g_asserts = 0;
    Thread other(
        [&]
        {
            CallOutput out;
            state->Call("echo", Span<const Value>(), Budget(), &out);  // not the owner
        });
    other.Join();
    CHECK(g_asserts >= 1);

    g_asserts = 0;
    state->Acquire();  // the creating thread already owns it
    CHECK(g_asserts == 1);
    SetAssertHandler(previous);
}
#endif

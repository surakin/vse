// Runs the backend conformance suite (Phase 27 Day 447) against script_expr, tests the backend registry and the
// `script.backend` setting, and proves the suite catches a planted fault.
#include <catch2/catch_test_macros.hpp>

#include "backend_conformance.h"
#include "script/backend_registry.h"
#include "script/expr/expr_backend.h"
#include "script/script_settings.h"
#include "settings/registry.h"

using namespace vse;
using namespace vse::script;
using namespace vse::script::test;

namespace
{

ConformanceKit ExprKit()
{
    ConformanceKit kit;
    kit.prelude = "def echo(a)\n  return a\nend\n"
                  "def multi()\n  return 1, 2.5, \"x\"\nend\n"
                  "def fail()\n  return 1 + nil\nend\n"
                  "def calls_native()\n  return host_add(2, 3)\nend\n"
                  "def waiter()\n  v = wait(0.25)\n  return v\nend\n"
                  "def setg(v)\n  global stored = v\nend\n"
                  "def getg()\n  return stored\nend\n"
                  "def spin()\n  while true\n  end\nend\n";
    kit.syntax_error = "def (";
    kit.hostile.Push("");
    kit.hostile.Push("))))((((");
    kit.hostile.Push("while true\nend");
    kit.hostile.Push("def f()\n f()\nend\nf()");
    kit.hostile.Push("x = \"unterminated");
    kit.hostile.Push("x = 99999999999999999999999999\ny = 1 / 0\nz = 5 % 0");
    kit.hostile.Push("def a(\n");
    kit.hostile.Push("\xff\xfe\x01 \x00 garbage");
    // Deep nesting must be refused, not recursed into until the stack ends.
    static const String<HeapAllocator> parens = []
    {
        String<HeapAllocator> text(HeapAllocator::Default(), StringView("x = "));
        for (int i = 0; i < 200000; ++i)
        {
            text.Append('(');
        }
        return text;
    }();
    static const String<HeapAllocator> blocks = []
    {
        String<HeapAllocator> text(HeapAllocator::Default(), StringView());
        for (int i = 0; i < 50000; ++i)
        {
            text.Append("if true\n");
        }
        return text;
    }();
    kit.hostile.Push(parens.CStr());
    kit.hostile.Push(blocks.CStr());
    return kit;
}

void Dump(const ConformanceReport& report)
{
    for (usize i = 0; i < report.failed.Size(); ++i)
    {
        UNSCOPED_INFO("failed: " << report.failed[i].CStr());
    }
}

// A backend that forwards to another and breaks one promise.
enum class Fault
{
    SwallowErrors,
    IgnoreBudgets,
    DropExtraResults,
};

class FaultyState : public State
{
public:
    FaultyState(UniquePtr<State> inner, Fault fault) : inner_(Move(inner)), fault_(fault) {}

protected:
    Outcome DoLoad(StringView n, StringView s) override { return inner_->Load(n, s); }
    Outcome DoCall(StringView p, Span<const Value> a, const Budget& b, CallOutput* out) override
    {
        Outcome o = inner_->Call(p, a, b, out);
        if (fault_ == Fault::SwallowErrors && o.status == Status::RuntimeError)
        {
            return Outcome();
        }
        if (fault_ == Fault::IgnoreBudgets && o.status == Status::BudgetExceeded)
        {
            return Outcome();
        }
        if (fault_ == Fault::DropExtraResults)
        {
            while (out->results.Size() > 1)
            {
                out->results.Pop();
            }
        }
        return o;
    }
    Outcome DoResume(u64 c, Span<const Value> v, const Budget& b, CallOutput* out) override
    {
        return inner_->Resume(c, v, b, out);
    }
    void DoAbandon(u64 c) override { inner_->Abandon(c); }
    Outcome DoRegisterFunction(StringView n, NativeFunction f, void* u) override
    {
        return inner_->RegisterFunction(n, f, u);
    }
    void DoCollect() override { inner_->Collect(); }
    usize DoMemoryUsed() override { return inner_->MemoryUsed(); }
    void DoClose() override { inner_->Close(); }
    Capabilities DoCapabilities() const override { return inner_->Caps(); }

private:
    UniquePtr<State> inner_;
    Fault fault_;
};

class FaultyBackend : public Backend
{
public:
    explicit FaultyBackend(Fault fault) : inner_(expr::CreateExprBackend()), fault_(fault) {}
    StringView Name() const override { return StringView("faulty"); }
    StringView Version() const override { return StringView("0"); }
    Capabilities Caps() const override { return inner_->Caps(); }
    UniquePtr<State> CreateState(u64 cap) override { return MakeUnique<FaultyState>(inner_->CreateState(cap), fault_); }

private:
    UniquePtr<Backend> inner_;
    Fault fault_;
};

}  // namespace

TEST_CASE("Conformance: script_expr passes every case", "[script][conformance]")
{
    UniquePtr<Backend> backend = expr::CreateExprBackend();
    const ConformanceReport report = RunConformance(*backend, ExprKit());
    Dump(report);
    CHECK(report.failed.Size() == 0);
    CHECK(report.passed >= 10);
    bool saw_memory_skip = false;
    for (usize i = 0; i < report.skipped.Size(); ++i)
    {
        if (report.skipped[i].View().StartsWith("memory_budget: "))
        {
            saw_memory_skip = true;
        }
    }
    CHECK(saw_memory_skip);
}

TEST_CASE("Conformance: a backend that swallows errors fails errors_are_values", "[script][conformance]")
{
    FaultyBackend backend(Fault::SwallowErrors);
    const ConformanceReport report = RunConformance(backend, ExprKit());
    CHECK(report.Failed("errors_are_values"));
    CHECK_FALSE(report.Failed("load_and_call"));
}

TEST_CASE("Conformance: a backend that ignores budgets fails instruction_budget", "[script][conformance]")
{
    FaultyBackend backend(Fault::IgnoreBudgets);
    const ConformanceReport report = RunConformance(backend, ExprKit());
    CHECK(report.Failed("instruction_budget"));
    CHECK(report.Failed("time_budget"));
    CHECK_FALSE(report.Failed("multiple_results"));
}

TEST_CASE("Conformance: a backend that drops results fails multiple_results", "[script][conformance]")
{
    FaultyBackend backend(Fault::DropExtraResults);
    const ConformanceReport report = RunConformance(backend, ExprKit());
    CHECK(report.Failed("multiple_results"));
    CHECK_FALSE(report.Failed("errors_are_values"));
}

TEST_CASE("Expr: the language runs programs", "[script][expr]")
{
    UniquePtr<Backend> backend = expr::CreateExprBackend();
    UniquePtr<State> state = backend->CreateState(0);
    REQUIRE(
        state
            ->Load("prog",
                   "def fib(n)\n  if n < 2\n    return n\n  end\n  return fib(n - 1) + fib(n - 2)\nend\n"
                   "def count()\n  i = 0\n  total = 0\n  while i < 10\n    total = total + i\n    i = i + 1\n  end\n"
                   "  return total, \"a\" + \"b\", 7 / 2, 7 % 4, not false and 1 < 2\nend\n")
            .IsOk());
    CallOutput out;
    Value arg[1] = {Value::FromInt(15)};
    REQUIRE(state->Call("fib", Span<const Value>(arg, 1), Budget(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromInt(610)));
    REQUIRE(state->Call("count", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 5);
    CHECK(out.results[0].Equals(Value::FromInt(45)));
    CHECK(out.results[1].Equals(Value::FromText("ab")));
    CHECK(out.results[2].Equals(Value::FromFloat(3.5)));
    CHECK(out.results[3].Equals(Value::FromInt(3)));
    CHECK(out.results[4].Equals(Value::FromBool(true)));
    REQUIRE(
        state
            ->Load(
                "w",
                "def w1()\n  wait_frames(1000000.0 * 1000000.0 * 1000000.0)\nend\ndef w2()\n  wait(1 / 0)\nend\ndef w3()\n  wait_frames(4294967296)\nend\n")
            .IsOk());
    for (const char* fn : {"w1", "w2", "w3"})
    {
        CHECK(state->Call(fn, Span<const Value>(), Budget(), &out).status == Status::RuntimeError);
    }
    const Outcome overflow = state->Load("deep", "def r()\n  return r()\nend\nr()");
    CHECK(overflow.status == Status::RuntimeError);
    CHECK(overflow.message.View().Contains("stack overflow"));
}

TEST_CASE("Registry: registers, finds, refuses duplicates and unknown names", "[script][registry]")
{
    ClearBackendRegistryForTest();
    CHECK(BackendCount() == 0);
    CHECK(expr::RegisterExprBackend());
    CHECK(expr::RegisterExprBackend());  // the same descriptor again is accepted and does not add an entry
    CHECK(BackendCount() == 1);
    CHECK(FindBackend("expr") != nullptr);
    CHECK(FindBackend("nope") == nullptr);

    BackendDescriptor clash;
    clash.name = "expr";
    clash.create = []() -> UniquePtr<Backend> { return UniquePtr<Backend>(); };
    CHECK_FALSE(RegisterBackend(clash));
    BackendDescriptor empty;
    empty.name = "";
    empty.create = clash.create;
    CHECK_FALSE(RegisterBackend(empty));
    BackendDescriptor no_create;
    no_create.name = "x";
    CHECK_FALSE(RegisterBackend(no_create));

    Outcome error;
    UniquePtr<Backend> made = CreateBackend("expr", &error);
    CHECK(made.Get() != nullptr);
    CHECK(error.IsOk());
    UniquePtr<Backend> missing = CreateBackend("lua", &error);
    CHECK(missing.Get() == nullptr);
    CHECK(error.status == Status::Invalid);
    CHECK(error.message.View().Contains("expr"));
    ClearBackendRegistryForTest();
}

TEST_CASE("Settings: script.backend defaults to luajit and is a registered setting", "[script][settings]")
{
    settings::Registry registry;
    RegisterScriptSettings(&registry);
    CHECK(ScriptBackendName(registry).View() == StringView("luajit"));
    CHECK(ScriptBudgeted(registry));
    CHECK(ScriptRandomSeed(registry) == 1);
}

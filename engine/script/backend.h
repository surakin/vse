// The language-backend interface (Phase 27 Day 446): what the engine asks of a scripting language, with no mention of
// Lua. The LuaJIT backend (Day 448) and the tiny test backend (Day 447) are two implementations; the engine, the
// bindings and the conformance suite see only these types.
#pragma once

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "script/status.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::script
{

/// What a call may use.
///
/// A zero field is unlimited. A backend that cannot honor a limit it is given reports `Status::Unsupported` for the
/// call; it never ignores it silently (see [[script::Capabilities]]).
///
/// - Thread-safety: thread-compatible plain data.
struct Budget
{
    /// The most virtual-machine instructions the call may run (0: unlimited).
    u64 instructions = 0;
    /// The most wall-clock nanoseconds the call may take (0: unlimited).
    u64 nanoseconds = 0;
    /// The most bytes the state may hold during the call (0: the state's own cap).
    u64 memory_bytes = 0;
};

/// What a backend can do, so a caller asks before it relies (and the conformance suite skips what a backend does not
/// advertise).
///
/// - Thread-safety: thread-compatible plain data.
struct Capabilities
{
    /// Whether the backend compiles hot code to machine code (a configuration of the backend, not of the language).
    bool jit = false;
    /// Whether a call can suspend and be resumed ([[WaitRequest]]).
    bool coroutines = false;
    /// Whether a chunk can be loaded from precompiled bytecode (an optional cache, never required).
    bool bytecode = false;
    /// Whether `Budget::instructions` is honored.
    bool instruction_budget = false;
    /// Whether `Budget::nanoseconds` is honored.
    bool time_budget = false;
    /// Whether `Budget::memory_bytes` and the state's memory cap are honored.
    bool memory_budget = false;
    /// Whether [[State::SetDebugSession]] works: line events, stack and locals inspection.
    bool debug = false;
    /// Whether [[State::SetProfiling]] and [[State::ProfileReport]] work.
    bool profiling = false;
    /// Whether [[State::Eval]] works.
    bool eval = false;
};

/// Why a call suspended, and what resumes it. The scheduler (Day 460) owns the clock: the backend only reports the
/// request.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the event name.
struct WaitRequest
{
    /// What the suspended call waits for.
    enum class Kind : u8
    {
        /// The call did not suspend.
        None,
        /// Simulation seconds.
        Seconds,
        /// A number of frames.
        Frames,
        /// A named event.
        Event,
        /// A parallel task (Day 461).
        Task,
    };

    /// The kind of wait; `None` means the call finished.
    Kind kind = Kind::None;
    /// For `Seconds`: how long.
    f64 seconds = 0.0;
    /// For `Frames`: how many.
    u32 frames = 0;
    /// For `Task`: the task id.
    u64 task = 0;
    /// For `Event`: the event name.
    String<HeapAllocator> event;

    /// Creates a request that is not a wait.
    WaitRequest() : event(HeapAllocator::Default()) {}
};

/// What a finished or suspended call produced.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the results.
struct CallOutput
{
    /// The values the function returned (empty when the call suspended).
    Vector<Value> results;
    /// The wait, when `wait.kind` is not `None`: the call is suspended and `coroutine` resumes it.
    WaitRequest wait;
    /// The id to pass to [[State::Resume]] or [[State::Abandon]]; 0 when the call finished.
    u64 coroutine = 0;

    /// Empties the output: no results, no wait, no coroutine. [[State::Call]] and [[State::Resume]] do this first, so
    /// an output can be reused.
    ///
    /// - Fails: never.
    void Reset()
    {
        results.Clear();
        wait = WaitRequest();
        coroutine = 0;
    }

    /// Whether the call is suspended and waiting to be resumed.
    ///
    /// - Fails: never.
    bool Suspended() const { return wait.kind != WaitRequest::Kind::None; }
};

/// Where the script that called a native function is: its chunk name and line.
///
/// - Thread-safety: thread-compatible plain data.
struct SourceLocation
{
    /// The chunk name, cut to fit and always ended with a zero.
    char chunk[48] = {};
    /// The line, 0 when unknown.
    u32 line = 0;
    /// Whether the chunk name was cut to fit: such a name does not identify a script (two names may share a prefix).
    bool truncated = false;
};

/// A backend's way to answer "where is the caller": called only while a native function runs on this thread.
///
/// - Returns: whether `out` was filled.
using SourceLocationFn = bool (*)(void* context, SourceLocation* out);

/// Publishes (or, with a null `fn`, withdraws) the caller-location provider for the current thread. A backend sets it
/// around each native call it makes, so a binding such as `log.info` can name the script and line; the provider is
/// asked only on demand, so the calls that do not ask pay for two stores.
///
/// - Thread-safety: thread-safe (a per-thread slot).
/// - Ownership: borrows `context`, which must live until the provider is withdrawn.
/// - Fails: never.
void SetCallerLocationProvider(SourceLocationFn fn, void* context);

/// Asks the provider of the current thread where the running native function was called from.
///
/// - Returns: whether a location was found; false outside a native call or when the backend has no provider.
/// - Ownership: fills `out`, which the caller owns.
/// - Thread-safety: thread-safe (a per-thread slot).
/// - Fails: false, as described.
bool CurrentCallerLocation(SourceLocation* out);

/// One frame of the script stack, as a debugger shows it.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the function name.
struct DebugFrameInfo
{
    /// The function's name as the language knows it (empty when unknown, `main chunk` for a chunk's top level).
    String<HeapAllocator> function{HeapAllocator::Default()};
    /// The chunk and line the frame is at.
    SourceLocation where;
};

/// One local variable of a frame.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its name and value.
struct DebugVariable
{
    /// The variable's name.
    String<HeapAllocator> name{HeapAllocator::Default()};
    /// A copy of its value; a value that is not a [[script::Value]] (a function, a userdata) is the text `<function>`
    /// and the like, and a table that is too deep or too large is cut.
    Value value;
};

/// What a debugger may ask while a script is stopped. It is valid only inside [[script::DebugSession::Pause]], on the
/// thread that is stopped.
///
/// - Thread-safety: thread-compatible: the stopped thread only.
/// - Ownership: owned by the backend.
class DebugInspector
{
public:
    /// Ends the inspector.
    virtual ~DebugInspector() = default;
    /// How many script frames are on the stack of the running coroutine (frame 0 is the innermost).
    ///
    /// - Fails: never.
    virtual usize FrameCount() = 0;
    /// Describes frame `level`.
    ///
    /// - Param level: 0 is the frame that is stopped.
    /// - Param out: receives the frame; unchanged on failure.
    /// - Returns: false when there is no such frame.
    /// - Ownership: `out` is borrowed for the call.
    virtual bool Frame(usize level, DebugFrameInfo* out) = 0;
    /// Lists the locals of frame `level` (parameters first, `self` among them for a behavior callback); the names the
    /// compiler makes for itself are left out.
    ///
    /// - Param level: the frame.
    /// - Param out: receives the variables (emptied first).
    /// - Returns: false when there is no such frame.
    /// - Ownership: `out` is borrowed for the call.
    virtual bool Locals(usize level, Vector<DebugVariable>* out) = 0;
};

/// What the engine does when a script reaches a line while a debugger is attached. A backend calls it on the thread
/// that runs the script, inside the language's hook, so it must be quick when it does not stop.
///
/// - Thread-safety: thread-compatible: the methods are called on the thread that owns the state; an implementation
/// shared by several states (several partitions) must be thread-safe.
/// - Ownership: borrowed by the state; it must outlive its use (detach with `SetDebugSession(nullptr)`).
class DebugSession
{
public:
    /// Ends the session.
    virtual ~DebugSession() = default;
    /// Whether the backend should report every line right now (a breakpoint is set, or a step or a pause is in
    /// progress). It is asked at the start of every call and after every stop.
    /// - Fails: never.
    virtual bool WantsLines() = 0;
    /// A line is about to run.
    ///
    /// - Param where: the chunk and line.
    /// - Param depth: how many script frames are on the stack.
    /// - Returns: true to stop here (the backend then calls [[script::DebugSession::Pause]]).
    virtual bool AtLine(const SourceLocation& where, u32 depth) = 0;
    /// The script is stopped at `where`; returns when it should run on. The thread blocks here, and may use
    /// `inspector` meanwhile. The call's wall-clock budget does not run while stopped.
    ///
    /// - Param where: the chunk and line.
    /// - Param depth: how many script frames are on the stack.
    /// - Param inspector: valid until this returns.
    /// - Ownership: `inspector` is borrowed until this returns.
    virtual void Pause(const SourceLocation& where, u32 depth, DebugInspector* inspector) = 0;
};

/// One function's profile: how often it ran and how long it took.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its names.
struct ProfileEntry
{
    /// The chunk the function was defined in (`[C]` for a native function).
    String<HeapAllocator> chunk{HeapAllocator::Default()};
    /// The function's name as the language knows it at its first call (may be empty).
    String<HeapAllocator> name{HeapAllocator::Default()};
    /// The line the function starts at (0 for a native function).
    u32 line = 0;
    /// How many calls were seen.
    u64 calls = 0;
    /// The nanoseconds from call to return, summed (including what the function called); calls that did not return
    /// normally (an error, a yield) add their count but no time.
    u64 nanoseconds = 0;
};

/// What a state's JIT compiler has done: whether it is on and how much machine code it holds.
///
/// - Thread-safety: thread-compatible plain data.
struct JitStats
{
    /// Whether the compiler is on in this state (false for a budgeted state, a state made with the JIT off, and a build
    /// without the compiler).
    bool enabled = false;
    /// The compiled traces the state holds now.
    u32 traces = 0;
    /// The bytes of machine code those traces hold.
    u64 code_bytes = 0;
};

/// A function the engine registers into a state for scripts to call.
///
/// It receives the pointer given to [[State::RegisterFunction]], copies of the call's arguments and an empty list to
/// fill with the values to return; it returns `Status::Ok`, or a failure status with a message that becomes a script
/// error in the calling script.
using NativeFunction = Outcome (*)(void* user, Span<const Value> args, Vector<Value>* results);

/// One language state: its globals, its loaded chunks, its memory.
///
/// **A state is used by one thread at a time**: it is created owned by the thread that
/// created it, and the owner changes only through [[State::HandOver]] and [[State::Acquire]], at a synchronization
/// point the caller provides (a job boundary). Every public call checks the owner in builds with debug checks.
///
/// A backend derives from `State` and implements the `Do...` functions; the public functions check the thread rules and
/// call them.
///
/// - Thread-safety: not-thread-safe by design: one owner thread at a time; the owner check and handover are the only
/// members safe to call from any thread.
/// - Ownership: the state owns its language runtime; destroying it closes the runtime.
class State
{
public:
    /// Closes the runtime if it is still open. The backend's destructor frees what it holds.
    virtual ~State() = default;

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    /// Compiles and runs `source` as a chunk named `chunk_name` (the name appears in messages and traces); its top
    /// level may define globals.
    ///
    /// - Returns: `Ok`, `SyntaxError` (message and trace say where), `RuntimeError`, `BudgetExceeded`, `OutOfMemory`,
    /// or `Invalid` for an empty name.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    Outcome Load(StringView chunk_name, StringView source)
    {
        CheckOwner();
        return DoLoad(chunk_name, source);
    }

    /// Compiles `source` as a chunk named `chunk_name` without running it, to check it and, when `bytecode` is not
    /// null, to produce the precompiled chunk a loader may use instead of the source (an optional cache of the
    /// backend's own format; the bytes are valid only for the same backend, version and architecture).
    ///
    /// - Returns: `Ok`, `SyntaxError` (message says where), `OutOfMemory`, `Invalid` for an empty name, or
    /// `Unsupported` from a backend that cannot compile ahead of time.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: `bytecode`, when given, is emptied first and then receives the chunk.
    Outcome Compile(StringView chunk_name, StringView source, Vector<byte>* bytecode)
    {
        CheckOwner();
        if (bytecode != nullptr)
        {
            bytecode->Clear();
        }
        return DoCompile(chunk_name, source, bytecode);
    }

    /// Loads and runs a host chunk, which must return a table of functions; each function is kept by the state under
    /// the table's key, outside the scripts' globals, so a script cannot replace it. [[State::Call]] reaches one with
    /// the path `@name`, and [[State::DefineModule]] uses `module_env` and `module_store` (the module host,
    /// docs/guide/scripting.md, "Minted scripts").
    ///
    /// - Param chunk_name: the chunk name for messages; not empty.
    /// - Param source: the host's text.
    /// - Returns: `Ok`, `SyntaxError`, `RuntimeError` (also when the chunk returns no table), `OutOfMemory`,
    /// `Invalid`, or `Unsupported` from a backend without hosts.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    Outcome LoadHost(StringView chunk_name, StringView source)
    {
        CheckOwner();
        return DoLoadHost(chunk_name, source);
    }

    /// Compiles `source` as a module without running it: the host's `module_env(key, deps)` makes the module's
    /// environment (its own `__require`, reading and writing the state's globals), the function gets that
    /// environment, and the host's `module_store(fn, key)` keeps it; the module runs when the host is asked
    /// (`Call("@module_run")`). No source text is spliced into a wrapper. The host must be loaded with
    /// [[State::LoadHost]] first.
    ///
    /// - Param key: the module's key (`require` names it); not empty.
    /// - Param chunk_name: the chunk name for messages; not empty.
    /// - Param source: the module's text.
    /// - Param deps: the keys the module may require, one per line (may be empty).
    /// - Returns: `Ok`, `SyntaxError`, `RuntimeError` when the host is missing or refuses the key, `OutOfMemory`,
    /// `Invalid`, or `Unsupported` from a backend without modules.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    Outcome DefineModule(StringView key, StringView chunk_name, StringView source, StringView deps)
    {
        CheckOwner();
        return DoDefineModule(key, chunk_name, source, deps);
    }

    /// Calls a script function with copies of `args`, within `budget`.
    ///
    /// `function_path` is a global name (or `@name`, a function of the host loaded with [[State::LoadHost]]), or
    /// dotted names into tables such as `game.on_update`.
    /// On success `out->results` holds the returned values; when the backend supports coroutines and the function
    /// suspends, `out->wait` says why and `out->coroutine` resumes it.
    ///
    /// - Returns: `Ok`, `RuntimeError` (including a missing function), `BudgetExceeded`, `OutOfMemory`, `Invalid` (a
    /// value over [[ValueLimits]], an
    ///   empty path), or `Unsupported` (a budget field the backend does not honor).
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: `out` is emptied first and then receives copies; the arguments are only read.
    Outcome Call(StringView function_path, Span<const Value> args, const Budget& budget, CallOutput* out)
    {
        CheckOwner();
        if (out == nullptr)
        {
            return Outcome(Status::Invalid, "Call needs an output");
        }
        out->Reset();
        return DoCall(function_path, args, budget, out);
    }

    /// Resumes the suspended call `coroutine` with `values` as the result of its wait, within `budget`.
    ///
    /// - Returns: as `Call`; `Invalid` when `coroutine` is not suspended in this state; `Unsupported` without the
    /// coroutine capability.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: `out` is emptied first and then receives copies; `values` are only read.
    Outcome Resume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out)
    {
        CheckOwner();
        if (out == nullptr)
        {
            return Outcome(Status::Invalid, "Resume needs an output");
        }
        out->Reset();
        return DoResume(coroutine, values, budget, out);
    }

    /// Drops the suspended call `coroutine` and frees what it holds; an id that is not suspended is ignored.
    ///
    /// - Aborts: the caller's thread does not own the state (debug checks).
    void Abandon(u64 coroutine)
    {
        CheckOwner();
        DoAbandon(coroutine);
    }

    /// Makes `function` callable from scripts under the global name `name` (or a dotted path into tables the backend
    /// creates).
    ///
    /// - Returns: `Ok`, `Invalid` for an empty name or a null function.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: the state keeps `function` and `user`; `user` must outlive the state.
    Outcome RegisterFunction(StringView name, NativeFunction function, void* user)
    {
        CheckOwner();
        return DoRegisterFunction(name, function, user);
    }

    /// Runs a full garbage collection (a backend with no collector does nothing).
    ///
    /// - Aborts: the caller's thread does not own the state (debug checks).
    void Collect()
    {
        CheckOwner();
        DoCollect();
    }

    /// The bytes the state holds now (0 for a backend that does not count).
    ///
    /// - Aborts: the caller's thread does not own the state (debug checks).
    usize MemoryUsed()
    {
        CheckOwner();
        return DoMemoryUsed();
    }

    /// Attaches a debugger session to this state (or, with null, detaches it). While attached and the session wants
    /// lines, the state reports every line to it, and it may stop the script ([[script::DebugSession]]).
    ///
    /// - Param session: the session, or null; borrowed until replaced.
    /// - Returns: `Ok`, or `Unsupported` from a backend without the `debug` capability.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: borrows `session` until it is replaced.
    Outcome SetDebugSession(DebugSession* session)
    {
        CheckOwner();
        return DoSetDebugSession(session);
    }

    /// Turns the per-function profile on or off. While on, every call and return of a function is timed (it costs time:
    /// off by default); turning it on starts from zero.
    ///
    /// - Returns: `Ok`, or `Unsupported` from a backend without the `profiling` capability.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    Outcome SetProfiling(bool on)
    {
        CheckOwner();
        return DoSetProfiling(on);
    }

    /// Copies the profile gathered so far, one entry per function.
    ///
    /// - Param out: receives the entries (emptied first).
    /// - Returns: `Ok`, or `Unsupported`.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: `out` is borrowed for the call.
    Outcome ProfileReport(Vector<ProfileEntry>* out)
    {
        CheckOwner();
        out->Clear();
        return DoProfileReport(out);
    }

    /// Reads what the state's JIT compiler has done.
    ///
    /// - Param out: receives the numbers (reset first).
    /// - Returns: `Ok`, or `Unsupported` when the language has no JIT.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: `out` is borrowed for the call.
    Outcome JitReport(JitStats* out)
    {
        CheckOwner();
        *out = JitStats();
        return DoJitReport(out);
    }

    /// Runs `source` as a console chunk: first as an expression (`return <source>`), and when that does not parse as a
    /// statement list, in `chunk_name`; the values it returns are copied to `out->results`. The chunk runs with the
    /// state's globals and no sandbox beyond the state's own.
    ///
    /// - Param chunk_name: the name for messages; not empty.
    /// - Param source: the chunk.
    /// - Param budget: the limits.
    /// - Param out: receives the results; not null.
    /// - Returns: `Ok`, `SyntaxError`, `RuntimeError`, `BudgetExceeded`, `OutOfMemory`, `Invalid`, or `Unsupported`.
    /// - Aborts: the caller's thread does not own the state (debug checks).
    /// - Ownership: borrows every argument for the call.
    Outcome Eval(StringView chunk_name, StringView source, const Budget& budget, CallOutput* out)
    {
        CheckOwner();
        if (out == nullptr)
        {
            return Outcome(Status::Invalid, "Eval needs an output");
        }
        out->Reset();
        return DoEval(chunk_name, source, budget, out);
    }

    /// Releases the language runtime now; the state is unusable afterwards except for `Close`, the owner check and the
    /// destructor.
    ///
    /// - Aborts: the caller's thread does not own the state (debug checks).
    void Close()
    {
        CheckOwner();
        DoClose();
    }

    /// What the backend of this state can do.
    Capabilities Caps() const { return DoCapabilities(); }

    /// Gives the state up: afterwards no thread owns it until one calls [[State::Acquire]].
    ///
    /// Call it as the last use before a synchronization point that
    /// publishes the state to another thread.
    ///
    /// - Aborts: the caller's thread does not own the state (debug checks).
    void HandOver()
    {
        CheckOwner();
        owner_.Store(kNoOwner, MemoryOrder::Release);
    }

    /// Takes the state on the calling thread, after the synchronization point that made it visible.
    ///
    /// - Aborts: another thread owns the state (debug checks).
    void Acquire();

    /// Whether the calling thread owns the state.
    ///
    /// - Fails: never.
    bool OwnedByCaller() const { return owner_.Load(MemoryOrder::Acquire) == CallerToken(); }

protected:
    /// Creates a state owned by the calling thread.
    State() : owner_(CallerToken()) {}

    /// See [[State::Load]].
    virtual Outcome DoLoad(StringView chunk_name, StringView source) = 0;
    /// See [[State::Compile]]; the default is `Unsupported`.
    virtual Outcome DoCompile(StringView chunk_name, StringView source, Vector<byte>* bytecode)
    {
        (void)chunk_name;
        (void)source;
        (void)bytecode;
        return Outcome(Status::Unsupported, "this backend cannot compile ahead of time");
    }
    /// See [[State::LoadHost]]; the default is `Unsupported`.
    virtual Outcome DoLoadHost(StringView chunk_name, StringView source)
    {
        (void)chunk_name;
        (void)source;
        return Outcome(Status::Unsupported, "this backend has no hosts");
    }
    /// See [[State::DefineModule]]; the default is `Unsupported`.
    virtual Outcome DoDefineModule(StringView key, StringView chunk_name, StringView source, StringView deps)
    {
        (void)key;
        (void)chunk_name;
        (void)source;
        (void)deps;
        return Outcome(Status::Unsupported, "this backend has no modules");
    }
    /// See [[State::SetDebugSession]]; the default is `Unsupported`.
    virtual Outcome DoSetDebugSession(DebugSession* session)
    {
        (void)session;
        return Outcome(Status::Unsupported, "this backend has no debugger");
    }
    /// See [[State::SetProfiling]]; the default is `Unsupported`.
    virtual Outcome DoSetProfiling(bool on)
    {
        (void)on;
        return Outcome(Status::Unsupported, "this backend has no profiler");
    }
    /// See [[State::ProfileReport]]; the default is `Unsupported`.
    virtual Outcome DoProfileReport(Vector<ProfileEntry>* out)
    {
        (void)out;
        return Outcome(Status::Unsupported, "this backend has no profiler");
    }
    /// See [[State::JitReport]]; the default is `Unsupported`.
    virtual Outcome DoJitReport(JitStats* out)
    {
        (void)out;
        return Outcome(Status::Unsupported, "this backend has no JIT");
    }
    /// See [[State::Eval]]; the default is `Unsupported`.
    virtual Outcome DoEval(StringView chunk_name, StringView source, const Budget& budget, CallOutput* out)
    {
        (void)chunk_name;
        (void)source;
        (void)budget;
        (void)out;
        return Outcome(Status::Unsupported, "this backend has no console");
    }
    /// See [[State::Call]].
    virtual Outcome DoCall(StringView function_path, Span<const Value> args, const Budget& budget, CallOutput* out) = 0;
    /// See [[State::Resume]]; a backend without coroutines returns `Unsupported`.
    virtual Outcome DoResume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out) = 0;
    /// See [[State::Abandon]].
    virtual void DoAbandon(u64 coroutine) = 0;
    /// See [[State::RegisterFunction]].
    virtual Outcome DoRegisterFunction(StringView name, NativeFunction function, void* user) = 0;
    /// See [[State::Collect]].
    virtual void DoCollect() = 0;
    /// See [[State::MemoryUsed]].
    virtual usize DoMemoryUsed() = 0;
    /// See [[State::Close]].
    virtual void DoClose() = 0;
    /// See [[State::Caps]].
    virtual Capabilities DoCapabilities() const = 0;

private:
    // A token for the thread: its id hash with the low bit set, never 0 (0 means nobody). Two threads could in
    // principle share a token; the check is a debugging aid for the ownership rule, not a lock.
    static usize CallerToken() { return ThisThread::Id().Hash() | 1u; }
    static constexpr usize kNoOwner = 0;

    void CheckOwner() const;

    Atomic<usize> owner_;
};

/// A language: a name, a version and the capabilities of its states, and a factory for states.
///
/// - Thread-safety: thread-safe: a backend is immutable after construction and `CreateState` may be called from any
/// thread.
/// - Ownership: a backend outlives the states it made unless its documentation says otherwise.
class Backend
{
public:
    /// Destroys the backend.
    virtual ~Backend() = default;

    /// The language's registry name (`luajit`, `expr`): lower case, no spaces.
    virtual StringView Name() const = 0;
    /// The language runtime's version text (for logs and bug reports).
    virtual StringView Version() const = 0;
    /// What this backend's states can do.
    virtual Capabilities Caps() const = 0;
    /// Creates an empty state. `memory_cap_bytes` limits what the state may hold (0: the backend's default cap); the
    /// calling thread owns the new state.
    ///
    /// - Returns: the state, or null when memory ran out.
    virtual UniquePtr<State> CreateState(u64 memory_cap_bytes) = 0;
};

}  // namespace engine_placeholder::script

#pragma once

//! [[net::Reactor]]: readiness and timers for sockets, on the job system.
//!
//! One dedicated thread waits in the platform's poller ([[net::Poller]]) for armed sockets and for the nearest timer. A
//! coroutine `co_await`s a wait ([[net::Reactor::WaitReadable]], [[net::Reactor::WaitWritable]],
//! [[net::Reactor::Sleep]]); when the wait ends, the reactor hands the coroutine to [[jobs::Scheduler::Schedule]], so
//! it resumes on a participant. The reactor thread runs no continuation of yours, and there is no thread per
//! connection.
//!
//! Every wait ends exactly once with one [[net::Status]]: `Ok` (the socket is ready to try again; the operation itself
//! then reports a real error), [[net::ErrorCode::TimedOut]], [[net::ErrorCode::Cancelled]] (a [[net::CancelToken]], or
//! the reactor was shut down), [[net::ErrorCode::Closed]] ([[net::Reactor::Forget]] or [[net::Reactor::Close]] ran for
//! the socket), or
//! [[net::ErrorCode::Invalid]] (the socket is not open, or another coroutine already waits for the same direction of
//! the same socket). The rules that make this safe:
//! - a socket that is already ready when the wait starts completes at once (the check is by level, not by edge);
//! - a cancel racing with a completion has one outcome, decided under the reactor's lock;
//! - a socket must be closed through [[net::Reactor::Close]] while coroutines may wait for it (closing the descriptor
//! behind the reactor's back would leave them waiting for ever);
//! - [[net::Reactor::Shutdown]] completes every wait with Cancelled.
//!
//! The reactor must be shut down (or destroyed) before the scheduler it was given, because it schedules the
//! continuations. On Linux the poller is epoll; on Windows it is a `select` poller.

#include "containers/hash_map.h"
#include "containers/vector.h"
#include "jobs/coroutine.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "net/cancel.h"
#include "net/clock.h"
#include "net/poller.h"
#include "net/signal.h"
#include "net/socket.h"
#include "net/status.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace vse::net
{

/// What a wait is for.
enum class WaitKind : u8
{
    Readable,  ///< The socket can be read, accepted from, or has hung up.
    Writable,  ///< The socket can be written, or a connect finished.
    Sleep,     ///< Only time passes: ends with Ok when the duration is over.
    Signal,    ///< A [[net::Signal]] fired by another thread.
};

/// Optional cancel token and timeout of a wait or an operation.
///
/// - Thread-safety: thread-compatible plain data.
struct WaitOptions
{
    CancelToken* token = nullptr;  ///< Cancels the wait; must outlive it; null for none.
    i64 timeout_nanos = -1;  ///< The longest the wait may last, in nanoseconds; negative for no timeout. Zero times out
                             ///< unless ready.
};

class Reactor;

/// The state of one wait; lives in the awaitable. Internal to the reactor: nothing outside it reads or writes these
/// fields.
///
/// - Internal: public only so that the awaitable can embed it.
/// - Thread-safety: thread-compatible; the reactor reads and writes it only under its lock.
struct Wait
{
    CoroutineHandle<> handle;    ///< The coroutine to resume.
    Reactor* reactor = nullptr;  ///< The reactor that holds the wait.
    i64 socket = -1;             ///< The socket's native handle, for Readable and Writable.
    WaitKind kind = WaitKind::Sleep;
    bool has_deadline = false;
    u64 deadline = 0;  ///< On the reactor's clock.
    CancelToken* token = nullptr;
    Signal* signal = nullptr;  ///< For WaitKind::Signal.
    Status result;             ///< The outcome, written before the coroutine is scheduled.
    bool done = false;         ///< Completed (guarded by the reactor's lock).
    Wait* prev = nullptr;      ///< The list of all active waits.
    Wait* next = nullptr;
    Wait* token_prev = nullptr;  ///< The list of the waits of one cancel token.
    Wait* token_next = nullptr;
    usize heap_index = ~usize{0};  ///< Position in the timer heap, or none.
};

/// The awaitable of a wait; `co_await` it from a job or a task to get the outcome as a [[net::Status]].
///
/// Create it with [[net::Reactor::WaitReadable]], [[net::Reactor::WaitWritable]] or [[net::Reactor::Sleep]], and await
/// it where it is created. It cannot be moved once awaited.
///
/// - Thread-safety: not-thread-safe. Belongs to the coroutine that awaits it.
/// - Ownership: holds the state of the wait until the coroutine resumes; the reactor borrows it meanwhile.
class WaitAwaitable
{
public:
    /// Tells the coroutine machinery to call [[net::WaitAwaitable::await_suspend]].
    /// - Returns: always false.
    bool await_ready() const noexcept { return false; }

    /// Registers the wait. If it cannot be registered (or ends at once) the coroutine goes on without suspending.
    /// - Param handle: the awaiting coroutine.
    /// - Returns: true if the coroutine suspends and will be resumed on a participant; false to go on at once.
    bool await_suspend(CoroutineHandle<> handle) noexcept;

    /// Gives the outcome.
    /// - Returns: the status the wait ended with.
    Status await_resume() const noexcept { return wait_.result; }

private:
    friend class Reactor;
    WaitAwaitable(Reactor* reactor, WaitKind kind, i64 socket, i64 timeout_nanos, CancelToken* token);
    friend class Signal;

    Wait wait_;
    i64 timeout_nanos_ = -1;
    bool invalid_ = false;  // created for a socket that is not open
};

/// Readiness and timers for sockets, resuming coroutines on the job system's participants.
///
/// - Thread-safety: thread-safe. Any thread may create waits, cancel them, forget sockets and shut the reactor down.
/// - Ownership: owns its thread and its poller, and joins the thread in the destructor. It borrows the scheduler (which
/// must outlive it) and the clock (likewise). Waits are owned by the coroutines that await them.
class Reactor
{
public:
    /// Starts the reactor thread.
    /// - Param scheduler: where the waiting coroutines are resumed; must outlive the reactor.
    /// - Param clock: the time source of timeouts and sleeps, or null for the real monotonic clock; must outlive the
    /// reactor.
    explicit Reactor(jobs::Scheduler& scheduler, const Clock* clock = nullptr);

    /// Shuts down as [[net::Reactor::Shutdown]] does and releases the poller.
    ~Reactor();

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    /// True when the poller was created and the thread runs.
    ///
    /// - Fails: never.
    bool IsRunning() const { return running_.Load(MemoryOrder::Acquire); }

    /// The scheduler the waiting coroutines resume on.
    /// - Returns: the scheduler given at construction.
    jobs::Scheduler& GetScheduler() const { return *scheduler_; }

    /// The clock of timeouts and sleeps.
    /// - Returns: the clock in use.
    const Clock& GetClock() const { return *clock_; }

    /// The id of the reactor thread, to tell it from a participant.
    /// - Returns: the thread's id.
    ThreadId GetThreadId() const { return thread_id_; }

    /// Waits until `socket` can be read, accepted from, or has hung up.
    /// - Param socket: an open socket; it must stay open (close it with [[net::Reactor::Close]]) until the wait ends.
    /// - Param options: token and timeout.
    /// - Returns: an awaitable giving the [[net::Status]] of the wait.
    WaitAwaitable WaitReadable(const Socket& socket, WaitOptions options = {});

    /// Waits until `socket` can be written, or a connect on it finished.
    /// - Param socket: an open socket; see [[net::Reactor::WaitReadable]].
    /// - Param options: token and timeout.
    /// - Returns: an awaitable giving the [[net::Status]] of the wait.
    WaitAwaitable WaitWritable(const Socket& socket, WaitOptions options = {});

    /// Waits for a duration on the reactor's clock.
    /// - Param nanos: the duration in nanoseconds.
    /// - Param options: token (the timeout field is ignored).
    /// - Returns: an awaitable that ends with Ok after `nanos`, or Cancelled.
    WaitAwaitable Sleep(u64 nanos, WaitOptions options = {});

    /// Waits until another thread fires `signal`.
    /// - Param signal: the signal; it must outlive the wait, and only one coroutine may wait for it.
    /// - Param options: token and timeout.
    /// - Returns: an awaitable giving Ok when the signal fires (also if it fired already), or TimedOut or Cancelled.
    WaitAwaitable WaitSignal(Signal& signal, WaitOptions options = {});

    /// Ends every wait on `socket` with [[net::ErrorCode::Closed]] and forgets the socket. Call it before the
    /// descriptor is closed by other means; [[net::Reactor::Close]] does both.
    /// - Param socket: the socket (open or not).
    void Forget(const Socket& socket);

    /// Ends the waits on `socket` with Closed, then closes it.
    /// - Param socket: the socket to close.
    /// - Fails: as [[net::Socket::Close]] ([[net::ErrorCode::Invalid]] for a socket that is not open).
    Status Close(Socket& socket);

    /// Makes the reactor look at its timers again; call it after moving a [[net::ManualClock]].
    void Poke() { poller_.Wake(); }

    /// Ends every wait with [[net::ErrorCode::Cancelled]], stops and joins the thread. Idempotent; later waits end at
    /// once with Cancelled. The scheduler must still be running: it resumes the cancelled coroutines.
    void Shutdown();

private:
    friend class WaitAwaitable;
    friend class CancelToken;
    friend class Signal;

    struct FdRecord
    {
        Wait* reader = nullptr;
        Wait* writer = nullptr;
    };
    using Ready = Vector<CoroutineHandle<>>;

    bool Begin(Wait* wait);
    void CancelWaitsOf(CancelToken* token);
    void FireSignal(Signal* signal);
    void ForgetLocked(i64 handle, Ready* ready);
    void RearmOrFail(i64 handle, Ready* ready);
    void LinkToken(Wait* wait);
    void UnlinkToken(Wait* wait);
    void Loop();
    void CompleteLocked(Wait* wait, Status result, Ready* ready);
    void Dispatch(const PollEvent& event, Ready* ready);
    void FireTimers(Ready* ready);
    void HeapPush(Wait* wait);
    void HeapRemove(Wait* wait);
    void HeapSiftUp(usize index);
    void HeapSiftDown(usize index);
    void LinkAll(Wait* wait);
    void UnlinkAll(Wait* wait);
    void Release(Ready* ready);
    static u32 InterestOf(const FdRecord& record);

    jobs::Scheduler* scheduler_;
    MonotonicClock real_clock_;
    const Clock* clock_;
    Poller poller_;
    Mutex lock_;  // guards everything below
    HashMap<i64, FdRecord, HeapAllocator> fds_;
    Vector<Wait*> heap_;
    Wait* all_ = nullptr;
    bool stopping_ = false;
    Status failure_;       // why the poller stopped, when it failed (otherwise waits end with Cancelled)
    Mutex shutdown_lock_;  // serializes Shutdown calls through the join
    Atomic<bool> running_{false};
    Thread thread_;
    ThreadId thread_id_;
};

}  // namespace vse::net

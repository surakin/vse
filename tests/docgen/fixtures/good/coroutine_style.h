#pragma once

#include <coroutine>

/// A coroutine-return-type style class.
class Task {
public:
    /// The promise type required by the coroutine machinery.
    struct promise_type {
        /// Creates the Task.
        Task get_return_object();
        /// Starts suspended.
        std::suspend_always initial_suspend() noexcept { return {}; }
        /// Allocates the coroutine frame.
        static void* operator new(unsigned long size);
        /// Frees the coroutine frame.
        static void operator delete(void* ptr) noexcept;
    };

    friend class Scheduler;

    static_assert(sizeof(void*) == 8, "64-bit only");
};

#pragma once

#include <coroutine>

//! Coroutine support (Phase 16): the engine's spelling of `<coroutine>`, the only header that includes it. The compiler
//! looks coroutine machinery up by the standard names (`std::coroutine_handle`, the awaiter members
//! `await_ready`/`await_suspend`/`await_resume`, the promise members
//! `initial_suspend`/`final_suspend`/`get_return_object`), so the aliases keep the standard member functions
//! (`resume`, `done`, `destroy`, `address`, `from_address`, `promise`, `from_promise`).
namespace engine_placeholder
{

/// A non-owning handle to a suspended coroutine whose promise type is `Promise`; `CoroutineHandle<>` is the type-erased
/// form
/// (`std::coroutine_handle`).
template <class Promise = void> using CoroutineHandle = std::coroutine_handle<Promise>;

/// An awaiter that always suspends (`std::suspend_always`).
using SuspendAlways = std::suspend_always;

/// An awaiter that never suspends (`std::suspend_never`).
using SuspendNever = std::suspend_never;

}  // namespace engine_placeholder

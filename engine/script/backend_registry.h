// The backend registry (Phase 27 Day 447): which languages a program has. A program registers the backends it links
// (`script::expr::RegisterExprBackend`, from Day 448 the LuaJIT one) and the `script.backend` setting names the one a
// world uses. The same pattern as the RHI's backend registry: explicit registration, no static initialization, a fixed
// table.
#pragma once

#include "containers/unique_ptr.h"
#include "script/backend.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// How a registry entry names and creates a backend.
///
/// - Thread-safety: thread-compatible plain data.
struct BackendDescriptor
{
    /// The language's name (`luajit`, `expr`): lower case, no spaces, at most 31 bytes. Not owned: a string literal.
    const char* name = nullptr;
    /// Makes a backend; may be called many times and from any thread.
    UniquePtr<Backend> (*create)() = nullptr;
};

/// The most backends a program can register.
inline constexpr usize kMaxBackends = 8;

/// Adds a backend to the registry. Registering the same name with the same `create` again is accepted and changes
/// nothing.
///
/// - Param descriptor: copied.
/// - Returns: true if the backend is registered (now or already).
/// - Fails: returns false when the name is empty or longer than 31 bytes, `create` is null, the table is full, or
/// another descriptor already holds the name.
/// - Thread-safety: thread-safe.
bool RegisterBackend(const BackendDescriptor& descriptor);

/// Finds the descriptor with this name (case-sensitive).
///
/// - Returns: the descriptor, or null when the program has no such backend.
/// - Ownership: the descriptor lives in the registry until [[script::ClearBackendRegistryForTest]].
/// - Thread-safety: thread-safe.
/// - Fails: null, as described.
const BackendDescriptor* FindBackend(StringView name);

/// How many backends are registered.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
usize BackendCount();

/// The `index`-th registered backend, in registration order, or null when `index` is not less than the count.
///
/// - Ownership: as `FindBackend`.
/// - Thread-safety: thread-safe.
/// - Fails: null, as described.
const BackendDescriptor* BackendAt(usize index);

/// Makes a backend by name. An unknown name is refused by value, never by abort.
///
/// - Param error: when not null, receives `Status::Invalid` and a message naming the backends there are, if the name is
/// unknown (or `Ok`).
/// - Returns: the backend, or null.
/// - Thread-safety: thread-safe.
/// - Ownership: the caller owns the returned backend.
/// - Fails: null, as described.
UniquePtr<Backend> CreateBackend(StringView name, Outcome* error);

/// Empties the registry. For tests only.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
void ClearBackendRegistryForTest();

}  // namespace engine_placeholder::script

#pragma once
//! Handles of the objects the C API hands out (Phase 23 Days 349 and 350): worlds, counters, loaded resources and load
//! tickets. A handle is kind (8 bits), generation (32 bits), slot + 1 (24 bits); the generation counts for the whole
//! process, so the handle of an object of an earlier engine is stale in a later one. Internal to the module `engine_c`.
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "engine_c/engine_c.h"
#include "engine_c/engine_internal.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

/// The kind byte of a handle.
enum HandleKind : u64
{
    kKindWorld = 1,
    kKindCounter = 2,
    kKindResource = 3,
    kKindTicket = 4,
    kKindImport = 5,
    kKindSurface = 6,
    kKindPixels = 7,
    kKindLastIssued = kKindPixels
};

/// The `struct_size` a host's struct starts with (the first field of every struct of the API).
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
inline u32 StructSizeOf(const void* p)
{
    u32 size = 0;
    MemCopy(&size, p, sizeof(size));
    return size;
}

/// The next generation number (never zero).
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
u32 NextGeneration();

/// The most recent generation number issued.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
u32 LastGeneration();

/// Makes the handle of slot `slot` of an object of `kind` made at `generation`.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
inline engine_handle MakeObjectHandle(HandleKind kind, u32 generation, usize slot)
{
    engine_handle h;
    h.value = (static_cast<u64>(kind) << 56) | (static_cast<u64>(generation) << 24) | static_cast<u64>(slot + 1);
    return h;
}

/// Finds the slot a handle of `kind` names. The caller holds the mutex that guards `slots`.
///
/// - Returns: OK, INVALID_ARGUMENT (zero, never issued), TYPE_MISMATCH (another kind), STALE_HANDLE (destroyed); the
///   failure text is set.
template <class Slot>
engine_status ResolveSlot(Vector<Slot>& slots, engine_handle handle, HandleKind kind, const char* what, Slot** out)
{
    *out = nullptr;
    if (handle.value == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the handle is zero");
    }
    const u64 handle_kind = handle.value >> 56;
    if (handle_kind > kKindLastIssued)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the handle was never issued");
    }
    if (handle_kind != static_cast<u64>(kind))
    {
        SetLastError("type_mismatch: the handle is not a %s", what);
        return ENGINE_ERROR_TYPE_MISMATCH;
    }
    const u32 generation = static_cast<u32>((handle.value >> 24) & 0xFFFFFFFFull);
    const u64 index_plus_one = handle.value & 0xFFFFFFull;
    if (generation == 0 || generation > LastGeneration() || index_plus_one == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the handle was never issued");
    }
    const usize index = static_cast<usize>(index_plus_one - 1);
    if (index >= slots.Size() || slots[index].generation != generation || slots[index].object.Get() == nullptr)
    {
        SetLastError("stale_handle: the %s was destroyed", what);
        return ENGINE_ERROR_STALE_HANDLE;
    }
    *out = &slots[index];
    return ENGINE_OK;
}

}  // namespace engine_placeholder::engine_c

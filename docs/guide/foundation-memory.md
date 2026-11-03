# Foundation: Memory

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0

## Source of truth

- `engine/foundation/mem/arena_allocator.h`
- `engine/foundation/mem/pool_allocator.h`
- `engine/foundation/mem/stack_allocator.h`
- `engine/foundation/os/os.h (ReserveMemory/ReleaseMemory)`

## Already known — seed material (verify against the code when writing)

- Custom allocators replace malloc in engine code; libc remains allowed and is used directly
- ArenaAllocator: bump allocation over reserved address space, Reset/FreeAll
- PoolAllocator is single-threaded, fixed-size, returns nullptr when full
- Jobs use their own thread-safe `FramePool`, not PoolAllocator

## Outline

### Choosing an allocator

_TODO_

### ArenaAllocator

_TODO_

### PoolAllocator

_TODO_

### StackAllocator

_TODO_

### Reserve vs commit (os::ReserveMemory)

_TODO_

### Failure behavior (nullptr vs assert)

_TODO_

### Thread-safety

_TODO_

### Common mistakes

_TODO_


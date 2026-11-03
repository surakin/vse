# Platform: OS Layer

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 1

## Source of truth

- `engine/foundation/os/os.h`
- `file_util.h`
- `command_line.h`
- `gpu_driver_probe.h`

## Already known — seed material (verify against the code when writing)

- os.h exposes libc/POSIX-shaped Open/Close/Read/Write/Lseek and Reserve/ReleaseMemory
- ReadFileToArena reads a whole file into an arena; returns nullptr on failure
- GPU driver probing loads libvulkan.so.1 via dlopen

## Outline

### Design: what is portable and what is per-platform

_TODO_

### Files

_TODO_

### Memory reservation

_TODO_

### Dynamic libraries and driver probing

_TODO_

### Command line

_TODO_

### Porting notes (Windows/macOS/mobile/console)

_TODO_


# Standard-library wrappers (Phase 16) — audit and design

**Status:** planning (2026-11-09). Day 207 confirms or amends this document with an exact, tool-made inventory.

## 1. Intent

The user's request (2026-11-09): audit every use of the C++ standard library, wrap each use in the engine's own functions and types, and leave **no file outside the wrapper modules that includes a standard header or spells `std::`**. Each category of use is wrapped in the existing module it belongs to, or in a new one; there is a gate that keeps it so.

Why: one place to port, replace, instrument and optimize each facility (a different mutex on a console, a different `Atomic` on a compiler without `<atomic>`, a custom `memcpy`), the same reason the types and containers were taken in Phase 10.

Non-goals: replacing the implementation (the wrappers call the standard library inside); changing behavior; touching `third_party/`; giving up what the compiler requires by name (see section 4).

## 2. Audit (measured 2026-11-09, `engine/ game/ tools/ tests/`; Day 207 redoes it with a tool)

Files that mention `std::` or include a standard header: foundation 69, platform 12, rhi 45, renderer 25, text 3, ui 30, jobs 18, game 16, tools 47, tests 194.

| Category | Headers | Typical uses (occurrences) | Files (engine/game/tools) |
|---|---|---|---|
| Concurrency | `<atomic>` 83 includes, `<thread>` 49, `<mutex>` 31, `<condition_variable>` 7 | `std::mutex` 349, `lock_guard` 307, `atomic` 295, `memory_order_*` ~480, `thread` 111, `this_thread` 53, `recursive_mutex` 17, `unique_lock` 12 | 83 (+52 in tests) |
| C strings and memory | `<cstring>` 217, `<cstdlib>` 102, `<cwchar>` 13, `<cctype>` | `strcmp` 334, `strlen` 134, `strstr` 102, `memcpy` 193, `memcmp` 58, `memset` 34, `strncmp`, `strchr`, `wcslen`, `free`, `atol`, `strtoul` | 173 |
| Formatted output | `<cstdio>` 150, `<cstdarg>` 13 | `snprintf` 209, `printf` 94, `fprintf` 93, `vsnprintf`, `swprintf`, `fputs`, `fflush` | (in the 173) |
| Scalar math | `<cmath>` 72 | `fabs` 97, `fmax` 66, `abs` 62, `sin`, `cos`, `sqrt`, `floor`, `ceil`, `pow`, `lround`, `isfinite` | 67 |
| Language support | `<utility>` 16, `<type_traits>` 11, `<limits>` 5, `<new>` 25, `<initializer_list>` 5, `<compare>` 1, `<concepts>` 1, `<cstddef>` 13, `<cstdint>` 6, `<climits>` | `move` 68, `is_same_v` 18, `is_convertible_v` 14, `numeric_limits` 9, `align_val_t` 21, `nothrow_t` 12, `initializer_list` 20, `strong_ordering` 8, `size_t` 161 | spread |
| Time | `<chrono>` 18, `<ctime>` 3 | `std::chrono` 65, `this_thread::sleep_for` | ~20 |
| Process, environment, signals | `<csignal>` 9, `<cerrno>` 15, `<exception>` 2, `<cstdlib>` | `getenv` 61, `abort` 16, `signal` 9, `errno` | ~40 |
| Coroutines | `<coroutine>` 7 | `coroutine_handle` 58, `suspend_always`, `coroutine_traits` | `engine/jobs` (9 files) + 4 tests |
| Containers and strings (Phase 10) | `<vector>` 54, `<string>` 33, `<map>` 3, `<memory>` 4, `<algorithm>` 10, `<tuple>`, `<string_view>`, `<fstream>`, `<charconv>` | `std::string` 820, `vector` 326, `to_string` 46, `sort` 18, `pair` 16, `make_unique` 9 | 0 in `engine/` and `game/`, 5 in `tools/`, 62 in `tests/` (the old directories allow-listed by `std_container_check`) |

## 3. Design

A module wraps the standard facilities of its own category. **The wrapper types are inline, zero-cost** (`constexpr`/`inline`, forwarding to the standard type or function), so a benchmark does not move. Names follow the engine style (`PascalCase`, `ENGINE_` macros).

| Category | Home | Layer | Wrapper |
|---|---|---|---|
| Language support | new `foundation/lang` | 0 | `Move`, `Forward`, `Swap`, `Exchange`, `IsSame`, `IsConvertible`, `EnableIf`, `RemoveReference`, `Decay`, `Limits<T>`, `Ordering`, `InitializerList<T>`, placement and aligned `new` helpers (`NewTag`, `AlignVal`, `NoThrow`) |
| Scalar math | `foundation/lang` (`scalar.h`) | 0 | `Sqrt`, `Sin`, `Cos`, `Abs`, `Floor`, `Ceil`, `Pow`, `FMin`, `FMax`, `LRound`, `IsFinite`, ... (log, mem and os use some, so they cannot sit in layer-4 `math`; Day 214 confirms by measuring which lower layers need them) |
| Concurrency | new `foundation/sync` | 0 | `Atomic<T>`, `MemoryOrder`, `Mutex`, `RecursiveMutex`, `LockGuard`, `UniqueLock`, `ConditionVariable`, `Thread`, `ThisThread` (`Yield`, `Id`) |
| Raw memory | `foundation/mem` | 2 | `MemCopy`, `MemMove`, `MemSet`, `MemCompare`, `Free`/`Allocate` (the C heap behind the heap allocator) |
| C strings, formatting, numbers | new `foundation/cstr` | 2 | `CStrLength`, `CStrCompare`, `CStrFind`, `CStrCopy`, `CStrAppend`, wide forms, `FormatTo(buffer, fmt, ...)`, `VFormatTo`, number parsing, character classes (log is layer 3 and needs formatting, so this cannot be `foundation/string`) |
| Console output | `foundation/os` | 1 | `os::Print`/`os::PrintError`/`os::Flush` over the existing `os::Write` |
| Process, environment, signals, errno | `foundation/os` | 1 | `os::Abort`, `os::Exit`, `os::GetEnvironment` (exists in part), `os::InstallSignalHandler`, `os::LastError` |
| Time | `foundation/os` | 1 | the clocks exist (`os::`); `Duration`/`TimePoint` value types and `os::Sleep` replace `std::chrono` and `sleep_for` |
| Coroutines | `engine/jobs` | 10 | `CoroutineHandle<P>`, `SuspendAlways`, `SuspendNever`; the `std::coroutine_traits` specialization lives only here |
| Containers and strings | existing | 4 | already wrapped by Phase 10; what remains is the tests and the five tools files |

Rules:

1. **Layering is kept** (`ARCHITECTURE.md`): a wrapper sits at or below its lowest user. `lang` and `sync` are layer 0 (no dependencies), `cstr` layer 2 next to `mem`.
2. **Several modules include the standard library**, each its own category; no module includes another module's headers. A gate (Day 220) lists, per module, the standard headers it may include, and fails any other file.
3. **Tests use the wrappers too** (decided with the user). Catch2 forces some `std::string` at its boundary; `tests/support/catch_engine_types.h` and `tools/shader_mint/reflect_json.cpp` (the nlohmann/json boundary) stay as documented exceptions.
4. **Third-party code is not touched.**
5. **Names the compiler requires stay in the wrapper modules** (section 4).

## 4. Hard parts and decisions (decided with the user)

- **Atomics and mutexes are wrapped with inline zero-cost types**, checked against the job-system and log benchmarks before and after (Days 209-210: baseline numbers recorded first; no regression beyond noise).
- **Compiler-mandated std names stay inside the wrapper modules:** `std::coroutine_traits` (jobs), `std::initializer_list` (lang; braces-initialization needs it), `std::strong_ordering` (lang; `operator<=>`), `std::align_val_t` and `std::nothrow_t` (lang; the global `operator new` overloads). Every other file spells the engine name.
- **C library calls are wrapped too** (`strlen`, `memcpy`, `snprintf`, ...), not only the C++ library: "only one module uses the std directly" includes the `std::` spelling of the C library.
- **No behavior change:** a wrapper forwards; where the standard behavior is undefined (e.g. `memcpy` with a null pointer) the wrapper may assert.
- **Windows-only files** (`engine/platform/windows`, `engine/rhi/dx12`, `win32_*`) cannot be built on Linux: they are ported on the Windows PC on Day 219; until then they keep using the standard library (still valid, because every wrapper is additive), and the gate lists them with that day as the removal date.
- **Unity builds and warnings-as-errors** apply to every wrapper header (`CLAUDE.md`).

## 5. Left to the builder

The exact function lists (Day 207 produces the inventory; a function used once may stay out), file layout inside each module, and the gate's token matching. Anything that changes what is built (a module name, a layer) goes back to the user.

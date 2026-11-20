# Standard types and container hygiene — design (Phase 10, Day 125)

Status: decided 2026-11-07 from the inventory and two spikes below. Scope: the foundation's type vocabulary (`byte`, `i128`/`u128`, `Span`) and the rule that `engine/`, `game/` and `tools/` use the engine's containers instead of `std::`
ones. `plans/phase-10-types-and-containers.md` holds the day list; this spec holds the reasons.

## 1. Goal

One vocabulary of types used everywhere, and a container rule that is enforced instead of hoped for. Raised by the user after Phase 9: the file layer's tests, `game/` and `tools/shader_mint` had `std::` uses, there was no `byte` and no 128-bit integer.

## 2. What "a banned `std::` use" means

**Banned** in `engine/`, `game/`, `tools/` (and, from Phase 9 on, in new test code except where Catch2 forces it): `std::string`, `std::string_view`, `std::wstring`, `std::vector`, `std::array`, `std::map`, `std::multimap`, `std::set`, `std::multiset`,
`std::unordered_map`, `std::unordered_set`, `std::deque`, `std::list`, `std::forward_list`, `std::queue`, `std::stack`, `std::priority_queue`, `std::bitset`, `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr`, `std::make_unique`, `std::make_shared`, `std::function`,
`std::optional`, `std::variant`, `std::span`, `std::pair`, `std::tuple`, `std::to_string`, `std::stringstream` and the other stream classes, and the `<algorithm>` functions (`std::sort`, `std::stable_sort`, `std::find`, `std::find_if`, `std::min`, `std::max`, `std::copy`, `std::fill`, `std::reverse`,
`std::unique`, `std::lower_bound`, ...), plus `#include` of `<string>`, `<vector>`, `<array>`, `<map>`, `<set>`, `<unordered_map>`, `<unordered_set>`, `<deque>`, `<list>`, `<queue>`, `<stack>`, `<memory>`, `<functional>`, `<optional>`, `<variant>`, `<span>`, `<tuple>`, `<algorithm>`,
`<sstream>`, `<iostream>`, `<fstream>`, `<filesystem>`, `<string_view>`.

**Allowed** (`PLAN.md` assumption #2 and `CLAUDE.md`): the concurrency primitives (`std::thread`, `std::this_thread`, `std::mutex`, `std::lock_guard`, `std::unique_lock`, `std::condition_variable`, `std::atomic`, `std::memory_order_*`, `<coroutine>`), the C library through the `std::` spelling
(`std::memcpy`, `std::strlen`, `std::snprintf`, `std::getenv`, `std::abort`, `std::qsort`, ...), `<cmath>` functions, `std::move`/`std::forward`/`std::size_t`/`std::nullptr_t`, `<type_traits>`/`<utility>`/`<new>`/`<cstdint>`/`<cstddef>`/`<limits>` language-support
facilities (`std::is_convertible_v`, `std::numeric_limits`, `std::align_val_t`), and `std::chrono` for time. Anything else is added to the gate's list or the allowed list with a reason when it first appears.

## 3. Inventory (2026-11-07)

**Engine runtime (`engine/`):** zero banned uses (checked by name and by include). The only `<algorithm>`-family call is `std::qsort` (C library, allowed). Nothing to port.

**`tools/docgen`, `tools/crash_extract` (after Phase 9), the CMake-script gates:** zero.

**`game/`** (10 uses): `game/common/textures.cpp` and `.h` (`std::vector<unsigned char>` for `Pixels::rgba`, `RtexImage::data`, the two writers' buffers), `game/rhi_demo/main.cpp` (`std::unique_ptr<UniformRing>`, `std::string` path, `std::vector<unsigned char>` pixel buffers),
`game/rhi_bench/main.cpp` (`std::vector<unsigned char>` upload data), `game/renderer_bench/main.cpp` (a `std::sort` and a `std::min`), `game/renderer_demo/main.cpp` (`unsigned char` buffers). All port on Day 127.

**`tools/shader_mint`** (about 140 uses): `reflect_json.cpp`/`.h` (53, the nlohmann/json boundary: **the one place `std::string`/`std::vector` stay**, because the library's API is built on them, `PLAN.md` assumption #19); `main.cpp` (43), `permutations.cpp` (42) and
`permutations.h` (14) have no JSON in them (text building, argument parsing, the permutation manifest) and port to `String`, `StringBuilder`, `Vector`, `HashMap` on Day 128.

**Phase 9 test code (ports on Day 128):** `tests/io/*` (126 uses: `async_file_test` 27, `directory_test` 33, `file_test` 19, `path_test` 16, `file_conformance.h` 16 incl. one `std::function`, `file_system_test` 6, `temp_dir.h` 5, `slow_file.h` 4), `tests/docgen/fs_fixture.h` (11),
`tests/rhi/image_compare.cpp` (7), plus the `/proc` readers added to `tests/jobs`. `std::thread`, `std::atomic` and `std::mutex` in them stay (allowed).

**Older tests (deferred, counted 2026-11-07):** `tests/docgen` 309, `tests/debug` 308, `tests/rhi` 192, `tests/renderer` 135, `tests/shader_mint` 72, `tests/jobs` 69, `tests/foundation` 2. They are driven by Catch2 and the file-system shapes they build; the rule for them is "convert a file when a day touches it".

**Byte spellings in `engine/`, `game/`, `tools/`:** `unsigned char`/`uint8_t` appears about 120 times in 30 files: `game/common/textures.*` (20), `engine/renderer/mesh.*` (11), `engine/foundation/os/linux/debug/crash_context.cpp` (5), `renderer/material.cpp`, `frame_data.*`, `jobs/frame_pool.*`,
`io/linux/directory.cpp`, `io/path.cpp`, `rhi/vulkan/vulkan_device_pipelines.cpp`, `vk_pipeline_policy.cpp`, `string/string.h`, `x11_window.cpp`, `docgen` scanners, `renderer_demo/demo_assets.cpp`, and a dozen single uses. Every one becomes `byte` (or `u8` where it is a small integer, not a buffer).
`unsigned short/int/long`, `long long` and `u?int(16|32|64)_t` outside `types.h` and generated protocol headers: about 70 in the Wayland/X11 backends (API types), `vk_memory.cpp`, `render_settings.cpp`, `rhi_bench`, `renderer_demo` (mostly `printf` argument casts).

## 4. The decisions

1. **`byte` is `using byte = u8;`** (confirmed). It is the one name for raw bytes. `isize` (`std::ptrdiff_t`) is added next to `usize`; `uptr`/`iptr` are **not** added (no use found in the inventory).
2. **`u128` and `i128` are portable structs** (confirmed by Spike A): `struct u128 { u64 lo; u64 hi; }`, little-endian halves, `alignas(16)` to match `unsigned __int128` on x86-64 and arm64 (a `static_assert` records `sizeof == 16`), trivially copyable. Where the build defines `ENGINE_HAS_NATIVE_INT128`
   (GCC and Clang) the operators use `unsigned __int128` inside; otherwise (and always with `ENGINE_FORCE_PORTABLE_INT128`) they use the portable algorithms below. `i128` is the two's-complement view of the same storage.
   - **Arithmetic contract:** `+ - *` and unary minus wrap modulo 2^128 for both types (signed overflow is defined here, unlike the builtin), `/` and `%` truncate toward zero, `i128` minimum divided by -1 wraps to the minimum with remainder 0, division or remainder by zero `Aborts:` (`ENGINE_ASSERT`).
   - **Shifts:** the count is a `u32`; a count of 128 or more gives 0 (or, for `i128 >>`, the sign fill), which is defined where the builtin's behavior is not; `>>` of `i128` is arithmetic.
   - **Conversions:** implicit from every smaller integer type (value-preserving), explicit narrowing, explicit to `f64`; no implicit conversion between `u128` and `i128`.
   - **Text:** `ToText(base)` for 2, 10 and 16, `FromText` with overflow detection; `Hash`; `CountLeadingZeros`, `CountTrailingZeros`, `PopCount`, `ByteSwap`.
   - **Portable algorithms (Spike A):** add/sub with carry on the halves; multiply by 32-bit partial products (`Mul64` to a 128-bit result, the cross terms added to the high half); shifts by half and by bits; division by Knuth algorithm D on 32-bit limbs with the single-limb and 64-bit fast paths.
   - **Spike A result:** 10 million random values per operator (biased toward 0, 1, 2^63, 2^64-1, 2^64, 2^127, 2^128-1 and mixed magnitudes) for add, sub, mul, div, mod, shl and shr against `unsigned __int128`: **0 mismatches in 60 million checks**. Speed on a multiply-add-modulo loop: native 4.3 ns, portable 16.3 ns (about 4 times slower,
     acceptable for the uses this has: ids, hashes, big counters, not inner loops).
3. **`Span<T>`, `ByteSpan = Span<const byte>`, `MutableByteSpan = Span<byte>`** (confirmed by Spike B): pointer and length, trivially copyable, no ownership. Implicit conversions (checked by the spike on the real `Vector`/`Array`): from `Vector<T>` and `const Vector<T>`, `Array<T, N>` and
   `const Array<T, N>` (needs its own constructor: found by the spike), `T[N]`, and `Span<U>` where only `const` is added; **const to mutable does not compile** (the spike's `Fill(const_span)` is rejected). `Data`, `Size`, `Empty`, `operator[]`, `begin`/`end` (pointers), `First(n)`, `Last(n)`, `Subspan(offset, count)`.
   `AsBytes(const T&)` for trivially copyable values, `AsByteSpan(StringView)` and `ToStringView(ByteSpan)` as named functions (no implicit text/bytes conversion).
   - **Bounds:** `operator[]`, `First`, `Last` and `Subspan` always `ENGINE_ASSERT` (changed on Day 126: `platform_neutral_check` forbids `ENGINE_CHECK_SLOW` in a header, since its setting differs per translation unit) (they are rare and the cost is one compare).
   - **The file layer** gets span overloads (`Read(MutableByteSpan, u64 offset, usize* bytes_read)`, `Write(ByteSpan, u64 offset)`, `Append(ByteSpan)`, `WriteFile(path, ByteSpan)`, `WriteFileAtomic`); the `(const void*, usize)` forms stay as thin wrappers because many callers have a pointer and a length already.
4. **Other facilities the ports need and the engine lacks** (built on Day 127, with tests, not worked around): `Sort`/`StableSort` over a `Span` with a comparison (`containers/sort.h`; docgen's local `StableSort` moves there), `Vector::Append(Span<const T>)`/`Assign`, `String::Reserve` if missing,
   `Min`/`Max` for the arithmetic types in `math_util.h`-style (`engine/foundation/algorithm.h` alongside `Sort`: `Min`, `Max`, `Clamp`, `IndexOf(Span, value)`), a `HashSet` only if a port needs one (none does in the inventory), no `Function` (test code takes a template parameter or a function pointer plus user pointer) and no `Optional`/`Pair`
   (none is needed: a status-and-out-parameter style is the project's, and a two-field struct names its fields).
5. **Scope of the rule** (confirmed): enforced by gate in `engine/`, `game/`, `tools/`; the `shader_mint` exception is exactly `reflect_json.cpp`/`.h`; tests written from Phase 9 on use the engine's types except where Catch2 forces `std::` (a comment says so); the older tests are the counted deferral above.
6. **Raw integer spellings:** the gate also rejects `unsigned char`, `signed char` and `(std::)u?int(8|16|32|64)_t` in `engine/`, `game/`, `tools/` outside `types.h`, the generated protocol headers and third-party code (there is no API-boundary case: `byte` *is* `unsigned char`, so it can be passed to any API that wants one). `unsigned short/int/long` and `long long` are ported where the project owns
   them but not gated (they appear as `printf` argument casts and in POSIX/X11/Wayland API types).
7. **The gate** `tools/std_container_check` (CMake script, ctest `std_container_check`, labelled to Phase 10): scans `engine/`, `game/`, `tools/` and the directories listed as clean (`tests/io`, `tests/types`, later `tests/settings`) for the banned names (`std::name` followed by a non-identifier character) and includes, ignoring comments and string literals; an allow list
   `tools/std_container_check/allowlist.txt` with `path|reason` per entry; a stale entry fails; the old test directories are one entry each, with their count in the reason, so the list only shrinks.

## 5. Class sketches

```cpp
namespace vse {
using byte = u8;  using isize = std::ptrdiff_t;

struct alignas(16) u128 { u64 lo = 0; u64 hi = 0; /* constexpr ctor from u64, operators, ToText, Hash, ... */ };
struct alignas(16) i128 { u64 lo = 0; u64 hi = 0; /* two's complement */ };

template <typename T> class Span {
public:
    constexpr Span() = default;
    constexpr Span(T* data, usize size);
    Span(Vector<U, A>&), Span(const Vector<U, A>&), Span(Array<U, N>&), Span(const Array<U, N>&), Span(U (&)[N]), Span(const Span<U>&);
    T* Data() const; usize Size() const; bool Empty() const;
    T& operator[](usize) const;  Span First(usize) const;  Span Last(usize) const;  Span Subspan(usize offset, usize count) const;
    T* begin() const;  T* end() const;
};
using ByteSpan = Span<const byte>;  using MutableByteSpan = Span<byte>;
template <typename T> ByteSpan AsBytes(const T&);  ByteSpan AsByteSpan(StringView);  StringView ToStringView(ByteSpan);
}
```

## 6. Open questions and deferrals

- The older tests' `std::` use (about 1090 uses): Catch2 prints engine types through `Catch::StringMaker` specializations written on Day 128 (`StringView`, `String`, `Path`, `Status`, `u128`, `Span`), which is also how a test file is converted later.
- The Windows build of the portable 128-bit path is verified by the cross-syntax compile only, until Phase 13.
- `f16`/`f128`: not planned.

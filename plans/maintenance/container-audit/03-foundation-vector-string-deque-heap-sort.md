# Container audit / Day 3: Foundation II: Vector, String, Deque, BinaryHeap and the sorting algorithms

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Day 2.

The other half of the missing features. The audit found order-keeping removes inside scans, copy loops, `Remove(0)` queues, a linear-scan priority
queue, insertion sorts with comments ("a handful", "tiny") that don't hold, and `qsort` through a function pointer on typed vectors.

## Tasks

- [x] **`Vector`:**
  - `SwapRemove(i)` (O(1), order not kept);
  - `Append(Span<const T>)` (one growth, then a copy: `MemCopy` for trivially copyable types);
  - `Clone()`, an explicit copy for copyable `T`, since the copy constructor stays deleted;
  - `Emplace(args...)`;
  - growth relocates trivially copyable elements with `MemCopy`;
  - `Swap(other)`.
  - Tests for each, including a `Push(v[0])`-style alias through `Append`.
- [x] **`String`:**
  - `Append`/`AppendRawBytes` reserve once and copy in bulk (no per-byte `Push`);
  - `Reserve(n)`;
  - `Clear()`;
  - `StringBuilder::Reserve`.
  - The Day 1 number for a 1 MB append, before and after.
- [x] **`Deque<T, Allocator>`** in `containers/deque.h`: a growable ring with `PushBack`, `PushFront`, `PopFront`, `PopBack`, `Front`, `Back`,
  `operator[]`, `Size`, `Clear` and `Reserve`. Move-only `T` works.
  - Tests: wrap-around, growth while wrapped, and move-only elements.
- [x] **`BinaryHeap<T, Less, Allocator>`** in `containers/binary_heap.h`: `Push`, `Pop`, `Top`, `Size`, `Clear`, and `Remove(index)` with a position callback
  so a caller can cancel an entry. The pop order is deterministic for a total order. Callers break ties by a sequence number, which the guide says.
  - Tests: heap order, ties under the sequence rule, and cancel.
- [x] **Algorithms** (`algorithm.h`):
  - `LowerBound`/`UpperBound`/`BinarySearch` over spans with a comparator, replacing the private copies (`settings/registry.cpp:104` and others, found by grep);
  - `NthElement` and `PartialSort` (introselect), for "keep the best k";
  - `StableSort` for movable `T` (scratch as a `Vector<T>`), not only trivially copyable `T`, so the per-frame insertion sorts can go;
  - `Sort` partitions three ways (or Hoare), so all-equal input stays n log n without falling to heap sort. Day 1 measured all-equal input; record before/after;
  - `Sort` accepts the RHI's read-only span through a conversion, so `metal_device_bindings.mm:233` needs no cast.
  - Tests against a brute-force oracle on random, sorted, reversed and all-equal inputs, with stability checked for `StableSort`.
- [x] **Range-for over every remaining container** (the user, 2026-11-19), as on Day 2: public lowercase `begin()`/`end()` with an `Internal:` label:
  - `Vector`: `T*`;
  - `String` and `StringView`: `const char*` over the bytes. The guide says it walks bytes, not code points, and points to the grapheme and UTF-8 iterators;
  - `Deque`: a `detail` iterator over the ring in front-to-back order;
  - `BinaryHeap`: none, because a heap's storage order means nothing. Record the decision;
  - `Array` and `Span` already have them: add the `Internal:` label so the reference treats all containers the same.
  - Tests: range-for on each container, including empty, const, and a wrapped `Deque`.
  - `UniquePtr`, `SharedPtr` and `SharedBytes` are not containers and get no `begin()`/`end()`.
- [x] **`SortRaw` (qsort)** stays for C-style tables only. The two typed per-frame uses are moved on Day 6.
- [x] **Docs:**
  - the "Choosing a container" table gains deque, heap and the algorithms;
  - `///` with contract labels;
  - `docgen lint` at zero;
  - `docs/reference` regenerated.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (everything), plain, then asan and tsan;
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - `Vector`: `SwapRemove`, `Append(ptr, count)` and `Append(range)` (anything with `Data()`/`Size()`; it copies from its own storage safely), `Emplace`,
    `Clone` (copyable `T`), `Swap`, `begin()`/`end()`; growth moves trivially copyable elements with `MemCopy`.
  - The free `Append(vector, span)` in `span.h` already existed: the audit's "no bulk append" was half true, since callers had not used it. It now
    forwards to the member.
  - `String`: bulk `Append`/`AppendRawBytes` (one growth, one copy), `Reserve`, `Clear`, `begin()`/`end()` over the bytes;
    `StringBuilder::Reserve`; `StringView::begin()`/`end()`.
  - `Deque` (`containers/deque.h`, a power-of-two ring, `ConstructAt` for placement) and `BinaryHeap` (`containers/binary_heap.h`, with `Remove(index)`
    and `At`; no begin/end).
  - `algorithm.h`: `LowerBound`/`UpperBound`/`BinarySearch` (over a span, const or not, or a container), `NthElement` (introselect), `PartialSort`,
    `StableSort` for any movable `T` (a `Vector` scratch; it aborts on out-of-memory instead of falling back to insertion), and `Sort`/`StableSort`
    of a container.
  - The `Internal:` label on the `begin()`/`end()` of `Array` and `Span`.
  - `tests/support/catch_engine_types.h`: `Catch::is_range<String<A>>` is false, so Catch2 prints a `String` as text, not as a range of chars.
- **Declined:**
  - A three-way `Sort` partition: Day 1 measured all-equal input at 8.4 ns per element against 42.3 ns for random input, so it does not degrade.
  - The private `LowerBound` copies (`settings/registry.cpp:104` and others): they move on their own days (10 and others), where each caller's comparison is checked.
- The RHI cast (`metal_device_bindings.mm:233`) was there because sorting a `Vector` meant building a foundation `Span`. `Sort(vector, less)` is the
  fix, and the call site is Day 11's (Metal is not built here).
- Pages touched: `docs/guide/foundation-containers.md` (at a glance, sorting and searching) and `docs/reference`.

## Runs

- **Deferred to Day 12** (the user's rule for the series). Built clean: the whole Clang unity `build` tree. While writing, the new and changed tests
  passed: `[deque],[binary_heap],[algorithm],[vector],[string],[hash_map],[hash_set]`, 47 test cases; the algorithms are checked against brute-force
  oracles on random, sorted, reversed, all-equal and few-distinct input.

## Post-implementation code review

`/code-review high`: 3 findings, all fixed:
1. `LowerBound`/`UpperBound`/`BinarySearch` took only `Span<const T>`, so a `Span<T>` or a `Vector` did not deduce. They now take `Span<T>` (const or
   not), with container overloads; there is a test.
2. `String::Append` still said it may not view its own storage; it may now, with a test.
3. A garbled guide sentence.

**Done:**
- `Vector` has swap-remove, bulk append, clone, emplace and swap.
- `String` appends in bulk.
- `Deque` and `BinaryHeap` exist.
- The algorithms have bounds, selection, a stable sort for any movable type, and a sort that stays fast on equal keys.
- Every piece is tested against an oracle and documented, with the Day 1 numbers re-taken for the changed paths.

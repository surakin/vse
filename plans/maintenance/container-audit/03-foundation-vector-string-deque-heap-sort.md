# Container audit / Day 3: Foundation II: Vector, String, Deque, BinaryHeap and the sorting algorithms

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Day 2.

The other half of the missing features. The audit found order-keeping removes inside scans, copy loops, `Remove(0)` queues, a linear-scan priority
queue, insertion sorts with comments ("a handful", "tiny") that don't hold, and `qsort` through a function pointer on typed vectors.

## Tasks

- [ ] **`Vector`:**
  - `SwapRemove(i)` (O(1), order not kept);
  - `Append(Span<const T>)` (one growth, then a copy: `MemCopy` for trivially copyable types);
  - `Clone()`, an explicit copy for copyable `T`, since the copy constructor stays deleted;
  - `Emplace(args...)`;
  - growth relocates trivially copyable elements with `MemCopy`;
  - `Swap(other)`.
  - Tests for each, including a `Push(v[0])`-style alias through `Append`.
- [ ] **`String`:**
  - `Append`/`AppendRawBytes` reserve once and copy in bulk (no per-byte `Push`);
  - `Reserve(n)`;
  - `Clear()`;
  - `StringBuilder::Reserve`.
  - The Day 1 number for a 1 MB append, before and after.
- [ ] **`Deque<T, Allocator>`** in `containers/deque.h`: a growable ring with `PushBack`, `PushFront`, `PopFront`, `PopBack`, `Front`, `Back`,
  `operator[]`, `Size`, `Clear` and `Reserve`. Move-only `T` works.
  - Tests: wrap-around, growth while wrapped, and move-only elements.
- [ ] **`BinaryHeap<T, Less, Allocator>`** in `containers/binary_heap.h`: `Push`, `Pop`, `Top`, `Size`, `Clear`, and `Remove(index)` with a position callback
  so a caller can cancel an entry. The pop order is deterministic for a total order. Callers break ties by a sequence number, which the guide says.
  - Tests: heap order, ties under the sequence rule, and cancel.
- [ ] **Algorithms** (`algorithm.h`):
  - `LowerBound`/`UpperBound`/`BinarySearch` over spans with a comparator, replacing the private copies (`settings/registry.cpp:104` and others, found by grep);
  - `NthElement` and `PartialSort` (introselect), for "keep the best k";
  - `StableSort` for movable `T` (scratch as a `Vector<T>`), not only trivially copyable `T`, so the per-frame insertion sorts can go;
  - `Sort` partitions three ways (or Hoare), so all-equal input stays n log n without falling to heap sort. Day 1 measured all-equal input; record before/after;
  - `Sort` accepts the RHI's read-only span through a conversion, so `metal_device_bindings.mm:233` needs no cast.
  - Tests against a brute-force oracle on random, sorted, reversed and all-equal inputs, with stability checked for `StableSort`.
- [ ] **Range-for over every remaining container** (the user, 2026-11-19), as on Day 2: public lowercase `begin()`/`end()` with an `Internal:` label:
  - `Vector`: `T*`;
  - `String` and `StringView`: `const char*` over the bytes. The guide says it walks bytes, not code points, and points to the grapheme and UTF-8 iterators;
  - `Deque`: a `detail` iterator over the ring in front-to-back order;
  - `BinaryHeap`: none, because a heap's storage order means nothing. Record the decision;
  - `Array` and `Span` already have them: add the `Internal:` label so the reference treats all containers the same.
  - Tests: range-for on each container, including empty, const, and a wrapped `Deque`.
  - `UniquePtr`, `SharedPtr` and `SharedBytes` are not containers and get no `begin()`/`end()`.
- [ ] **`SortRaw` (qsort)** stays for C-style tables only. The two typed per-frame uses are moved on Day 6.
- [ ] **Docs:**
  - the "Choosing a container" table gains deque, heap and the algorithms;
  - `///` with contract labels;
  - `docgen lint` at zero;
  - `docs/reference` regenerated.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (everything), plain, then asan and tsan;
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:**
- `Vector` has swap-remove, bulk append, clone, emplace and swap.
- `String` appends in bulk.
- `Deque` and `BinaryHeap` exist.
- The algorithms have bounds, selection, a stable sort for any movable type, and a sort that stays fast on equal keys.
- Every piece is tested against an oracle and documented, with the Day 1 numbers re-taken for the changed paths.

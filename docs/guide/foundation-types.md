# Foundation: Standard Types

Status: draft (Phase 10, Day 126: the types; the container rule arrives with the gate on Day 129).

The engine has one vocabulary of types, declared in `engine/foundation/types.h`, `int128.h` and `containers/span.h`. Code the project owns uses these names, never the raw standard ones (`unsigned char`, `uint8_t`, `long long`).
Design and reasons: `docs/superpowers/specs/2026-11-07-types-and-containers-design.md`.

## Which type do I use?

| I need | Use |
|---|---|
| a raw byte, a buffer of bytes | `byte` (an alias of `u8`), `Vector<byte>`; a view of bytes is `ByteSpan` (read) or `MutableByteSpan` (write) |
| a view of any run of elements | `Span<T>` / `Span<const T>`: from a `Vector`, an `Array`, a C array or a pointer and a length |
| text | `StringView` (view), `String` / `StringBuilder` (owned) |
| 8, 16, 32, 64-bit integers | `i8`, `i16`, `i32`, `i64` and `u8`, `u16`, `u32`, `u64` |
| 128-bit integers | `i128`, `u128` (below) |
| an object size or an index | `usize`; the difference of two pointers is `isize` |
| floating point | `f32`, `f64` |
| true or false | `bool` |

## Bytes and spans

`byte` is the same type as `u8`, so a byte buffer can be handed to any API that takes `unsigned char`. A `Span<T>` is a pointer and a length: it owns nothing and costs nothing to pass by value. It converts implicitly from a `Vector`,
an `Array` (also const), a C array and another span that only adds `const`; a read-only view never converts back to a writable one. `operator[]`, `First`, `Last` and
`Subspan` always check their bounds (unlike `Vector`'s unchecked `operator[]`). `AsBytes(value)` views a trivially copyable object as bytes, `AsByteSpan(text)` and `ToStringView(bytes)` convert between text and bytes by name, never implicitly.

```cpp compile run
Vector<byte> buffer;
buffer.Resize(6);
MutableByteSpan out(buffer);
for (usize i = 0; i < out.Size(); ++i) {
    out[i] = static_cast<byte>(10 * (i + 1));
}
ByteSpan view = buffer;                      // writable to read-only is implicit
ByteSpan middle = view.Subspan(2, 3);        // 30 40 50
usize sum = 0;
for (byte b : middle) {
    sum += b;
}
u32 header = 0x04030201u;
std::printf("%zu %zu %u\n", sum, AsBytes(header).Size(), static_cast<unsigned>(AsBytes(header)[0]));
```
```output
120 4 1
```

## 128-bit integers

`u128` and `i128` are classes of two 64-bit halves (low half first, 16-byte aligned), so they behave identically on every compiler; MSVC has no `__int128`. Where the compiler has one, the operators use it inside; elsewhere they use portable
algorithms that are tested against it (`ENGINE_HAS_NATIVE_INT128` says which this build uses). `u128` converts implicitly from unsigned integers of up to 64 bits and `i128` from signed or unsigned ones (write `5u`, not `5`, for a `u128`; `u128(5)` also works only for unsigned types), and both explicitly to other integers. They support every arithmetic, shift, bit and comparison operator, and print with `ToText`
(bases 2 to 16) and parse with `FromText`.

Where the builtin is undefined, these are defined: `+ - *` and unary minus **wrap** (also for `i128`); a shift by 128 or more gives 0 (or the sign fill for `i128 >>`); `/` and `%` truncate toward zero, and the minimum `i128` divided
by -1 wraps to itself. Dividing by zero aborts. They are about four times slower than `__int128` for multiplication and division, which is fine for ids, hashes and counters.

```cpp compile run
u128 big = u128(1u) << 100;
big += u128(12345u);
char text[u128::kMaxTextBytes];
big.ToText(text, sizeof(text));
std::printf("%s\n", text);

i128 min = i128::Min();
char signed_text[i128::kMaxTextBytes];
(min + i128(1)).ToText(signed_text, sizeof(signed_text));
std::printf("%s %d\n", signed_text, (-min == min) ? 1 : 0);

u128 parsed;
bool ok = u128::FromText("ffffffffffffffffffffffffffffffff", 16, &parsed);
std::printf("%d %d\n", ok ? 1 : 0, parsed == u128::Max() ? 1 : 0);
```
```output
1267650600228229401496703217721
-170141183460469231731687303715884105727 1
1 1
```

# Foundation: Strings and Unicode

Status: written (Phase 0).

**Audience:** every engine programmer. Read [Standard types](foundation-types.md), [Memory](foundation-memory.md) and
[Containers](foundation-containers.md) first: the string types are built from them.

The engine has no `std::string`. It has three small types, and a handful of functions for working with Unicode. All text in the engine is **UTF-8**.

| I need | Use | Owns the characters? |
|---|---|---|
| to look at text (a parameter, a sub-range, a literal) | [[StringView]] | no |
| text that I keep, that is built up or changed | [[String]]`<Allocator>` | yes |
| to build text from pieces (numbers, characters, other strings) | [[StringBuilder]]`<Allocator>` | yes |

The rule of thumb: **functions take `StringView`, objects that keep text hold a `String`.**

## StringView: look, don't own

A `StringView` is a pointer and a length. It never copies, it does not need a terminating `\0`, and it converts implicitly from a string literal or a
C string. It is cheap to pass by value.

```cpp compile run standalone
#include "os/console.h"
#include "string_view.h"

using namespace engine_placeholder;

int main() {
    StringView path = "assets/models/tree.mesh";

    const usize slash = path.LastIndexOf('/');                  // Length() when absent
    StringView file = path.Substring(slash + 1);                // shares the same memory
    StringView folder = path.Substring(0, slash);

    os::Print("file '%.*s' in '%.*s'\n", static_cast<int>(file.Length()), file.Data(), static_cast<int>(folder.Length()), folder.Data());
    os::Print("ends with .mesh: %s, contains 'models': %s\n", file.EndsWith(".mesh") ? "yes" : "no", path.Contains("models") ? "yes" : "no");
    os::Print("equal to a literal: %s\n", file == StringView("tree.mesh") ? "yes" : "no");
    return 0;
}
```

```output
file 'tree.mesh' in 'assets/models'
ends with .mesh: yes, contains 'models': yes
equal to a literal: yes
```

What to remember:

- **Searches return `Length()` when nothing is found**, not -1: `IndexOf`, `LastIndexOf` and `Find` all do (`Contains`, `StartsWith`, `EndsWith` return a `bool`).
- **Everything is bytewise.** `Length()` is a number of *bytes*, and `==` and `<` compare bytes. See "UTF-8" below for counting characters.
- **A view does not own what it looks at.** Do not store one in an object that outlives the text, and do not return a view of a local `String`.
- **Not NUL-terminated.** `file.Data()` is not safe to pass to a C function that expects a terminated string; use `String` and `CStr()` for that.
- `Substring` is not bounds-checked in release builds.

## String: owned text

A [[String]] holds its characters in memory from an allocator you give it and is always NUL-terminated. It is built from an allocator (and optionally a
`StringView` to copy), and grows with `Append`.

```cpp compile run standalone
#include "mem/heap_allocator.h"
#include "os/console.h"
#include "string/string.h"

using namespace engine_placeholder;

int main() {
    HeapAllocator& heap = HeapAllocator::Default();

    String<HeapAllocator> name(heap, "player");
    name.Append('_');
    name.Append("one");
    os::Print("%s has %zu bytes\n", name.CStr(), name.Length());

    StringView view = name;                    // converts to a view for free
    os::Print("starts with 'player': %s\n", view.StartsWith("player") ? "yes" : "no");
    return 0;
}
```

```output
player_one has 10 bytes
starts with 'player': yes
```

- The allocator must outlive the string, and `String` never frees through it by itself beyond what the allocator does (a heap string frees; an arena string is
  reclaimed when the arena is reset). Use an arena for text that lives one frame, the heap for text that you keep.
- Any `Append` may move the characters: **a view or a `CStr()` pointer taken before an append is not valid after it.**
- A moved-from `String` is empty and may only be assigned to or destroyed.
- Copying a string is explicit: `String<A> copy(allocator, original.View());`.

## StringBuilder: formatting without printf

`printf` is not available (it is a C-library function the engine wraps). To build a line of text from numbers and pieces use a [[StringBuilder]]; every
`Append...` returns the builder so calls chain:

```cpp compile run standalone
#include "mem/arena_allocator.h"
#include "os/console.h"
#include "string/string.h"

using namespace engine_placeholder;

int main() {
    ArenaAllocator scratch(512);                      // throwaway memory for one line
    StringBuilder<ArenaAllocator> line(scratch);

    line.Append("entity ").AppendUInt(42).Append(" at (").AppendFloat(1.5, 2).Append(", ").AppendFloat(-0.25, 2).Append(") flags=0x").AppendHex(255, 4);
    os::PrintLine(line.CStr());

    line.Append(" ").AppendInt(-7).AppendCodepoint(0x00E9);   // an 'é', encoded as UTF-8
    os::PrintLine(line.CStr());
    return 0;
}
```

```output
entity 42 at (1.50, -0.25) flags=0x00ff
entity 42 at (1.50, -0.25) flags=0x00ff -7é
```

The builder has `Append(StringView)`, `Append(char)`, `AppendCodepoint`, `AppendInt`, `AppendUInt`, `AppendHex(value, min_digits)` and `AppendFloat(value,
precision)`. For the occasional formatted message, the logging macros (see [Logging](foundation-logging-and-assert.md)) already format for you, and
`os::Print` takes a `printf`-style format for console output.

## UTF-8

Text is a sequence of **bytes**, but a person sees **characters**. In UTF-8 a character (a *codepoint*) takes 1 to 4 bytes: plain ASCII is one byte, `é` is
two, most Asian characters three, emoji four. So `Length()` is not "how many letters".

```cpp compile run standalone
#include "os/console.h"
#include "string/utf8.h"
#include "string/utf8_codec.h"

using namespace engine_placeholder;

int main() {
    StringView text = "caf\xC3\xA9 \xF0\x9F\x8E\x89";    // "café 🎉"

    os::Print("bytes %zu, codepoints %zu, valid %s\n", text.Length(), CodepointCount(text), IsValidUtf8(text) ? "yes" : "no");

    for (u32 codepoint : Utf8Codepoints(text)) {          // decode one codepoint at a time
        os::Print("U+%04X ", codepoint);
    }
    os::Print("\n");

    char bytes[4];
    const usize count = EncodeUtf8(0x20AC, bytes);        // the euro sign
    os::Print("U+20AC is %zu bytes\n", count);

    StringView broken("a\xFF" "b", 3);                     // 0xFF is never valid UTF-8
    os::Print("broken: valid %s, codepoints %zu\n", IsValidUtf8(broken) ? "yes" : "no", CodepointCount(broken));
    return 0;
}
```

```output
bytes 10, codepoints 6, valid yes
U+0063 U+0061 U+0066 U+00E9 U+0020 U+1F389 
U+20AC is 3 bytes
broken: valid no, codepoints 3
```

Malformed input never crashes or loops forever: each bad byte is read as **U+FFFD** (the "replacement character") and decoding moves on, so you can
safely display text from a file or a user. Check `IsValidUtf8` where you need to reject bad input instead. `EncodeUtf8` writes a replacement character for a
surrogate or an out-of-range value. Never split a string in the middle of a multi-byte character: slice at codepoint starts (iterate to find them).

## Grapheme clusters: what a user calls a character

Even a codepoint is not always "one thing on screen": `e` followed by a combining accent `´` is two codepoints that show as one `é`; a flag is two regional
indicator codepoints; a family emoji is several joined by invisible joiners. A **grapheme cluster** is the unit a person would select with the cursor or delete
with one backspace. The engine implements the Unicode rules (UAX #29, Unicode 17.0.0):

```cpp compile run standalone
#include "os/console.h"
#include "string/grapheme.h"
#include "string/utf8.h"

using namespace engine_placeholder;

int main() {
    StringView text = "e\xCC\x81" "x";               // 'e' + combining acute accent, then 'x'

    usize clusters = 0;
    for (StringView cluster : GraphemeClusters(text)) {
        os::Print("cluster of %zu bytes\n", cluster.Length());
        ++clusters;
    }
    os::Print("%zu codepoints, %zu clusters\n", CodepointCount(text), clusters);
    return 0;
}
```

```output
cluster of 3 bytes
cluster of 1 bytes
3 codepoints, 2 clusters
```

Use clusters for anything a user sees as a position: moving a text cursor, backspace, truncating a label with "...". The text-editing code of the UI
([UI](ui.md), [Text](text.md)) is built on them.

## Normalization: comparing text that looks the same

The same visible text can be stored in different ways: `é` can be one codepoint (U+00E9) or `e` plus U+0301. A bytewise `==` says they differ.
**Normalization** converts text to a standard form so equal text has equal bytes: [[NormalizeNFC]] (composed, what you should store and compare) and
[[NormalizeNFD]] (decomposed). Both take an allocator for the result and a view of the text, and use a temporary scratch buffer internally.

```cpp compile run standalone
#include "mem/heap_allocator.h"
#include "os/console.h"
#include "string/normalize.h"
#include "string/string.h"

using namespace engine_placeholder;

int main() {
    HeapAllocator& heap = HeapAllocator::Default();
    StringView composed = "\xC3\xA9";                // U+00E9
    StringView decomposed = "e\xCC\x81";             // U+0065 U+0301

    os::Print("raw equal: %s\n", composed == decomposed ? "yes" : "no");

    String<HeapAllocator> a = NormalizeNFC(heap, composed);
    String<HeapAllocator> b = NormalizeNFC(heap, decomposed);
    os::Print("after NFC equal: %s (%zu bytes)\n", a.View() == b.View() ? "yes" : "no", b.Length());
    return 0;
}
```

```output
raw equal: no
after NFC equal: yes (2 bytes)
```

Normalize at the boundary (when text comes in from a file or the keyboard) if you compare or search names. Hangul syllables are handled by an algorithm; everything
else comes from tables generated from the Unicode data.

## The Unicode tables

The grapheme and normalization tables under `engine/foundation/string/generated/` are generated, checked in, and not built from anything at compile time.
The pinned Unicode version is in `UNICODE_VERSION` (17.0.0) and the source data files are vendored in `tools/unicode_data_gen/downloads/`. To move to a newer Unicode
version follow `tools/unicode_data_gen/README.md` and commit the new data and the regenerated tables together. The tool is never part of a shipped binary.

## Common mistakes

- **Treating `Length()` as a character count.** It counts bytes; use `CodepointCount` or grapheme clusters.
- **Keeping a `StringView` of a temporary or of a `String` you later append to.** Copy into a `String` if the text must outlive its source.
- **Passing `view.Data()` to a function that expects a terminated C string.** A view is not terminated; build a `String` and use `CStr()`.
- **Slicing at an arbitrary byte.** You can cut a multi-byte character in two; slice at codepoint (or cluster) boundaries.
- **Comparing user-entered names with `==` without normalizing.** Normalize to NFC first.
- **Using `printf`, `std::string` or `std::to_string`.** They are not available; use `StringBuilder`, `os::Print` or the log macros.

## See also

[Standard types](foundation-types.md), [Containers](foundation-containers.md), [Text](text.md); reference: [Foundation: Strings](../reference/foundation-strings.md).

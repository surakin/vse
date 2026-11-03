# Foundation: Strings and Unicode

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0

## Source of truth

- `engine/foundation/string/string.h`
- `utf8.h`
- `utf8_codec.h`
- `grapheme.h`
- `normalize.h`

## Already known — seed material (verify against the code when writing)

- StringView (non-owning), String, StringBuilder are hand-rolled
- UTF-8 throughout; grapheme clustering follows UAX #29 (Unicode 17.0.0 tables, generated)
- NFC/NFD normalization tables generated
- Text cursor/line-break positions will reuse grapheme clustering (Phase 5)

## Outline

### StringView / String / StringBuilder

_TODO_

### UTF-8 encoding and decoding

_TODO_

### Grapheme clusters

_TODO_

### Normalization (NFC/NFD)

_TODO_

### Regenerating the Unicode tables

_TODO_

### Common mistakes

_TODO_


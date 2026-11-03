# docgen — Design Specification

Settled on Day 36 of Phase 2.5. Days 37-43 implement exactly this; change
it here first if implementation shows a decision was wrong. The author-facing
rules (comment grammar, labels, visibility) are in `docs/DOC-CONVENTIONS.md`.

## 1. Scope and non-goals

A dev-time tool in C++20 built on the foundation layer (strings, UTF-8,
`os::` file I/O, arenas). Built with the production flags (`-fno-exceptions
-fno-rtti`), linked into nothing the engine ships. It is **not** a C++
parser: it recognizes the declaration shapes in section 3 and *refuses
loudly* on anything else.

## 2. Layout

```
tools/docgen/
  DESIGN.md
  CMakeLists.txt        # targets: docgen_lib, docgen
  src/                  # scanner, extractor, comment parser, emitters, checkers, main
tests/docgen/
  fixtures/good/*.h     # must scan/extract with zero diagnostics
  fixtures/bad/*.h      # must produce exactly the annotated diagnostics
  *_test.cpp            # Catch2
docs/reference/
  modules.txt           # module map (section 4)
  symbols.txt           # generated symbol index
  *.md                  # generated pages
```

## 3. Supported C++ (the scanner's contract)

Recognized, public-surface relevant:

- `namespace n {}`, `namespace a::b {}`, anonymous and `inline` namespaces,
  `extern "C" {}`.
- `class`/`struct`/`union` (with `final`, base lists, nested types),
  `enum` and `enum class` (with underlying type; enumerators with optional
  `= value` and `///<` docs).
- Access labels `public:`/`protected:`/`private:`.
- Functions, methods, constructors, destructors, operators, conversion
  operators; specifiers `constexpr consteval inline static virtual
  override final explicit noexcept = default = delete = 0`; attributes
  `[[...]]`; trailing return types; `requires` clauses.
- Templates: `template<...>` on classes, functions, aliases, variables
  (header text kept verbatim).
- Variables: `static`, `inline`, `constexpr`, `static constexpr` members.
- `using X = ...;` aliases, `typedef`, `using namespace` (ignored).
- Function-like and object-like `#define` that carry a doc comment.
- Bodies, member initializers, lambdas and default arguments are skipped by
  brace/paren matching.
- `friend` declarations and `static_assert` are skipped.

**Refused with an error** (never guessed): declarations produced by macro
invocation at namespace/class scope (e.g. `DECLARE_THING(x)` outside a
documented `#define`), unbalanced braces, unterminated comments/strings,
and a `///` comment attached to something that is not a declaration.

Also refused (as `unrecognized declaration`): several declarators in one
statement (`int a, b;`) and declarators after a type definition
(`struct S {...} s;`) — declare each name in its own statement.

Known limit: `Widget w(args);` (direct initialization with identifier
arguments) is indistinguishable from a function declaration without type
information and is read as a function; `int v(5);` (a literal argument) is
read as a variable. Prefer `=` or braces for variables in headers.

## 4. Module map (`docs/reference/modules.txt`)

Line-based, `#` comments, blank lines ignored.

```
module jobs "The Job System"        # name, page title (one per module)
  header engine/jobs/*.h            # glob relative to repo root (* and ** only)
  exclude engine/jobs/jobs_log.h    # optional, repeatable
  strict thread-safety              # optional lint rules (section 8), repeatable
  strict ownership
```

Modules are rendered in file order; headers within a module in
lexicographic path order; declarations in source order.

## 5. Symbol IDs and anchors

ID = qualified name; for functions append the **normalized parameter type
list** and trailing qualifiers: `engine_placeholder::jobs::Counter::Increment(u64)`,
`jobs::Scheduler::HelpUntil(const std::atomic<int>&,int)`. Normalization: collapse
whitespace, drop parameter names and default values, keep `const`/`&`/`*`,
trailing `const`/`noexcept` kept. The outermost `engine_placeholder::`
namespace is **omitted** in IDs and display (`jobs::Counter::Increment(u64)`).

Anchor = ID lowercased with every run of non-alphanumeric characters
replaced by one `-` and trimmed, e.g. `jobs-counter-increment-u64`. The
punctuation that tells overloads and special members apart is first spelled
out as a word so anchors stay distinct and stable when members are
reordered: `~` tilde, `=` eq, `!` not, `<` lt, `>` gt, `+` plus, `-` minus,
`*` star, `/` slash, `%` pct, `&` amp, `|` pipe, `^` caret, and the literal
`operator()` / `operator[]` become `operator-call` / `operator-index`
(`V::~V()` is `v-tilde-v`, `Read(int,int*)` is `read-int-int-star`). Ties
that remain get `-2`, `-3` suffixes in source order.

Parameter types in IDs are written without spaces except between two words
(`const char*`, `unsigned long`); names and default values are dropped, also
inside function-pointer parameter lists. A function declared and then
defined inline in the same header is one symbol (the first declaration's
location, the documented one's comment); any other repeated ID is a
`duplicate symbol ID` error.

## 6. Output formats

### Reference page (one per module, deterministic)

````markdown
# The Job System

_Generated by docgen. Do not edit._

<file overview from //! comments, per header, in order>

## Counter

`class jobs::Counter` — defined in `engine/jobs/counter.h`

```cpp
class Counter { ... }            // signature only, no bodies
```

<summary>

<details>

| Field | Value |
|---|---|
| Thread-safety | thread-safe. ... |
| Ownership | ... |

### `Increment(u64)` {#jobs-counter-increment-u64}
...
````

Members are `###` under their class; enumerators render as a table. A
"See also" line lists `See:` references as links.

### Symbol index (`docs/reference/symbols.txt`)

One symbol per line, tab-separated: `ID`, `kind`, `module`, `anchor`,
`page`, `header:line`. Sorted by module order then source order. This is the
file the link checker and the in-editor help consume.

### Staleness

`docgen reference` rewrites `docs/reference/`. `docgen check` regenerates in
memory and compares to disk; exit 1 if any file differs, listing them.
Generated files carry no timestamps. A page counts as generated (and may be
deleted by `docgen reference`, or reported by `check` once it is no longer
produced) only if its third line is exactly `_Generated by docgen. Do not
edit._` after the title and a blank line; hand-written pages in the same
directory are left alone. `--out` is created if missing, including parents.

## 7. CLI and diagnostics

```
docgen reference [--root DIR] [--modules FILE] [--out DIR]
docgen check     [same]
docgen links     [--root DIR]
docgen samples   [--root DIR] [--build-dir DIR] [--jobs N]
docgen lint      [--baseline FILE] [--root DIR]
docgen dump      FILE.h        # declaration tree (testing aid)
docgen docs      FILE.h        # parsed doc comments + validation (testing aid)
common: --keep-going  --quiet
```

Diagnostics: `path:line: error|warning|note: message` on stderr (column
omitted). Exit codes: `0` clean (warnings allowed unless `--werror`), `1`
at least one error (including a stale `check`), `2` usage or I/O failure
(unreadable module map or output directory). Output is deterministic;
diagnostics are sorted by path then line.

Diagnostic catalogue (stable message prefixes the fixtures assert):

| Prefix | Severity |
|---|---|
| `unterminated comment` / `unterminated string literal` | error |
| `unbalanced braces` | error |
| `unrecognized declaration` | error |
| `macro-generated declaration` | error |
| `detached doc comment` | warning |
| `unknown label` | error |
| `duplicate label` | error |
| `Param name does not match a parameter` | error |
| `Returns on a void function` | warning |
| `missing summary` | error |
| `header pattern matches no files` / `header listed in two modules` | error |
| `duplicate symbol ID` | error |
| `unresolved reference` / `ambiguous reference` (in `///` comments; Day 41 adds guide pages) | error |
| `empty label` | error |
| `invalid thread-safety category` | error |
| `malformed label` (e.g. `See:` without a `[[reference]]`) | error |
| `malformed reference` (missing `]]`, empty `[[]]`) | error |
| `unterminated code fence` | error |
| `doc comment is not attached to a declaration` | error |
| `summary too long` / `summary does not end in a period` | warning |
| `undocumented public symbol` | lint error |
| `missing required label` | lint error |

Position rules: a diagnostic about one labeled line (`unknown label`,
`duplicate label`, `Param ...`, `Returns ...`) is reported at that line;
`missing summary` and `detached doc comment` at the first line of the
comment; lexical errors at the offending construct's first line;
`unbalanced braces` at the line of the unclosed `{`; declaration errors at
the declaration's first line.

## 8. Lint rules (Day 43)

- Every public symbol has a doc comment with a summary.
- Per-module `strict` rules: `thread-safety` (public class/struct needs
  `Thread-safety:`), `ownership` (functions returning/taking raw owning
  pointers, or classes owning resources, need `Ownership:`), `failure`
  (functions returning `bool`/pointer or marked `[[nodiscard]]` need
  `Returns:`/`Fails:`).
- Coverage must not drop below the committed baseline.

## 9. Fixture expectations

A good fixture (`fixtures/good/*.h`) must produce zero diagnostics. A bad
fixture (`fixtures/bad/NAME.h`) has a sibling `NAME.h.expect` listing, one per
line, the diagnostics it must produce *exactly* (no more, no fewer):

```
<line>: <error|warning>: <message prefix>
```

Expectations live in a separate file so no annotation comment can disturb
doc-comment attachment. The declaration-tree goldens (`*.tree`) for good
fixtures are generated on Day 38 once the dump format exists.

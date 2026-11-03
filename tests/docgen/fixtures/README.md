# docgen fixtures

`good/` headers must scan and extract with **zero diagnostics**; their
declaration-tree goldens (`*.tree`) are generated on Day 38. `bad/` headers
must produce exactly the diagnostics listed in their sibling `NAME.h.expect`
file (`<line>: <error|warning>: <prefix>`; see `tools/docgen/DESIGN.md`
section 9).

| Fixture | Covers |
|---|---|
| good/namespaces.h | nested `a::b`, anonymous, `detail`, `inline` namespaces, `extern "C"` |
| good/classes.h | class/struct, access, nested types, ctors/dtors, `= default/delete`, virtual/override/final/pure |
| good/enums.h | `enum`, `enum class`, underlying types, `///<` enumerator docs |
| good/functions.h | free functions, attributes, `noexcept`, trailing return, `constexpr`, operators, conversion operators |
| good/templates.h | class/function/alias templates, `requires`, variadics |
| good/variables_aliases.h | `inline constexpr`, `static constexpr` members, `using`, `typedef` |
| good/macros.h | documented object-like and function-like `#define`, undocumented one |
| good/tricky_lexing.h | `//` in strings, `/*` in char literals, raw strings with `)`, `#if 0`, line continuation, digit separators |
| good/contract_labels.h | every contract label |
| good/file_overview.h | `//!` overview and `override` summary inheritance |
| good/bodies.h | inline bodies, lambdas, brace initializers, nested braces |
| good/coroutine_style.h | nested `promise_type`, static `operator new/delete`, `friend`, `static_assert` |
| good/crlf.h | CRLF line endings |
| bad/unterminated_comment.h | `unterminated comment` |
| bad/unterminated_string.h | `unterminated string literal` |
| bad/unbalanced_braces.h | `unbalanced braces` |
| bad/unknown_label.h | `unknown label` |
| bad/duplicate_label.h | `duplicate label` |
| bad/param_mismatch.h | `Param name does not match a parameter` |
| bad/macro_generated_decl.h | `macro-generated declaration` |
| bad/detached_doc.h | `detached doc comment` (warning) |
| bad/missing_summary.h | `missing summary` |
| bad/returns_on_void.h | `Returns on a void function` (warning) |

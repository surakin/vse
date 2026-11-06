# Phase 3 / Day 36 — Design Spec & Doc-Comment Convention

**Phase:** [Phase 3 — Documentation Tooling](../phase-03-docs-tooling.md)
**Depends on:** Phase 2 complete (nothing else).

Planning-grade day: fixes the contract every later day implements. No tool code yet beyond fixtures.

## Tasks

- [x] Write the **doc-comment grammar** into `docs/DOC-CONVENTIONS.md`: `///` blocks attach to the next declaration; `//!` blocks describe the file; first paragraph = summary; the rest = details; blank `///` line separates paragraphs; fenced ``` blocks and `-` lists allowed.
- [x] Define the fixed **contract labels** (a line starting `Thread-safety:`, `Ownership:`, `Fails:`, `Aborts:`, `Returns:`, `Param <name>:`, `Example:`, `See:`), their allowed values/shape, and which declaration kinds they apply to.
- [x] Define **cross-reference syntax** `[[Qualified::Name]]` (and `[[Qualified::Name(overload-hint)]]`) and the **symbol ID scheme** (qualified name; overloads disambiguated by parameter-type text; stable across runs).
- [x] Define the **scope of C++ the scanner supports** (namespaces, class/struct/union/enum[ class], access specifiers, functions/methods/ctors/dtors/operators, templates, aliases, constexpr/static data members, documented `#define`s, friend/default/delete) and the list of things it must refuse loudly rather than guess (e.g. macro-generated declarations, K&R-style oddities).
- [x] Decide **what is public**: namespace-scope declarations in headers under the module map, plus `public:` members; `detail::`/anonymous namespaces and `private`/`protected` excluded; an explicit `/// @internal`-style opt-out label `Internal:`.
- [x] Specify the **tool layout**: `tools/docgen/` (library `docgen_lib` + executable `docgen`), built on the foundation (strings, UTF-8, `os::` file I/O, arenas); no exceptions/RTTI; CLI (`docgen reference|check|links|samples|lint`), exit codes, and diagnostics format `file:line: severity: message`.
- [x] Specify the **module map** file (`docs/reference/modules.txt`): module name -> header globs, ordering, page title.
- [x] Specify the **generated Markdown format** for reference pages (per-symbol heading, signature block, summary, labeled contract table, see-also) and decide generated output is **committed** under `docs/reference/` with a `check` mode that fails when it is stale.
- [x] Write the **acceptance corpus**: `tests/docgen/fixtures/` with ~12 small headers covering every supported construct and ~6 deliberately broken ones, each with the expected diagnostics.
- [x] Add the phase to `PLAN.md`'s roadmap table, `STATUS.md`, and the cross-references in `docs/`.

**Done:** the convention and design are written down precisely enough that Days 37-43 can be implemented without further design decisions, and the fixture corpus exists.

## Outcome

- Author-facing convention: `docs/DOC-CONVENTIONS.md` (comment forms,
  labels and thread-safety categories, visibility, inheritance,
  cross-references, samples, worked example).
- Tool design: `tools/docgen/DESIGN.md` (layout, supported C++ and the
  refuse list, module map grammar, symbol IDs/anchors, output formats, CLI,
  diagnostic catalogue with position rules, lint rules, fixture contract).
- Fixture corpus: `tests/docgen/fixtures/` — 13 good headers and 10 bad
  headers with `.expect` files (`README.md` lists coverage). Declaration
  tree goldens are produced on Day 38.
- Roadmap, `STATUS.md` and `docs/` cross-references were updated when the
  phase was added.
- Decisions made while writing: expectations live in sibling `.expect`
  files (inline annotation comments would split/detach doc comments);
  plain `//` comments never carry meaning; the leading `engine_placeholder::`
  namespace is omitted from symbol IDs; thread-safety is a fixed category
  vocabulary; `override` members inherit the base summary.
- Planning-only day: no `/code-review` (CLAUDE.md exempts plan/STATUS-only
  commits).

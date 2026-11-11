# The database interface: SQL behind one door

Status: in progress (Phase 19). Built: the SQLite library (Day 251), the interface and its conformance suite (Day 252). Next: the SQLite backend on `:memory:`
(Day 253), its file system and locks (Day 254), settings, cache and concurrency (Day 255).

**Audience:** engine programmers who keep structured data in tables (the storage layer, a game's save index, tools). Design and the portable SQL subset:
`docs/superpowers/specs/2026-11-11-storage-design.md`.

The module `engine/database` (namespace `database`) is the only place SQL is spoken. Code above it holds a `database::Database`, opens
`database::Connection`s, prepares `database::Statement`s and never names a backend. SQLite (in memory or on a file) is the one backend; there is no second, because it would need its own SQL parser. The conformance suite defines what a
backend must do, and the portable statement subset of the spec keeps another backend possible.

## The vocabulary

| Type | What it is |
|---|---|
| `Status` | The result of every call: an `ErrorCode` (`Busy`, `Constraint`, `Syntax`, ...), the backend's own code and a message cut to 119 bytes. Allocates nothing. |
| `SqlText` | SQL that is known at compile time. Only a string literal converts to it; a `StringView` or `String` does not. Values go through parameters. |
| `ValueView`, `Value` | A column or parameter value: null, integer, real, text or blob. `ValueView` borrows its bytes; `Value` owns a copy. Equality is bitwise (`-0.0` is not `0.0`). |
| `Database` | A backend. `Open(options, &connection)`; `Supports(feature)`. |
| `Connection` | One session: `Prepare`, `Execute`, transactions, savepoints, `SchemaVersion`. Not thread-safe; one connection per thread. |
| `Statement` | A prepared statement: `Bind` (parameters count from 1), `Step`, `Column` (from 0), `Reset`. |
| `Transaction` | A guard: begins in the constructor, rolls back in the destructor unless `Commit` succeeded. |

```cpp
database::Transaction tx(*connection);
if (tx.BeginStatus().Failed()) { return tx.BeginStatus(); }
UniquePtr<database::Statement> insert;
connection->Prepare("INSERT INTO kv (k, v) VALUES (?, ?)", &insert);
insert->Bind(1, database::ValueView::Integer(7));
insert->Bind(2, database::ValueView::Text("seven"));
bool row = false;
insert->Step(&row);
return tx.Commit();
```

## Rules the interface keeps

- **SQL text is a literal.** `SqlText` has a consteval constructor, so injection by concatenation cannot compile. The one escape, `SqlText::Unchecked`,
  is for a backend generating a savepoint statement from a validated name; the gate `database_sql_check` fails on it anywhere else.
- **The portable subset.** `CREATE TABLE`/`INDEX`, `INSERT` (also `OR REPLACE`, multi-row), `SELECT` with equality or range `WHERE` joined by `AND`,
  `ORDER BY`, `LIMIT`/`OFFSET`, `UPDATE`, `DELETE`, transactions and savepoints. No joins, subqueries, `ALTER TABLE` text or `PRAGMA` text; the schema
  version is `Connection::SchemaVersion`.
- **Optional features** are asked with `Supports(Feature)`: savepoints, `WITHOUT ROWID`, add-column, named parameters, concurrent readers, write-ahead
  log, foreign keys. Using an unsupported one returns `Unsupported` (or `Syntax` for SQL text), never silently something else.
- **A failed statement does not end the transaction**; the caller decides to roll back.
- **Misuse is a status, not an abort**: a bind index out of range, `Commit` without `Begin`, a nested `Begin`, a `Step` after the last row without `Reset`.

## Writing a backend

Implement `Database`, `Connection` and `Statement`, then instantiate the conformance suite in a test file:

```cpp
struct MyFixture {
    static constexpr bool kImplemented = true;
    static const char* Name() { return "mine"; }
    Database& Db();
    OpenOptions Options() const;   // the same shared location every call
};
DATABASE_CONFORMANCE_CASES(MyFixture)
```

`tests/database/conformance.h` holds the cases; `tests/database/conformance_stub_test.cpp` shows an unimplemented backend, whose cases skip.

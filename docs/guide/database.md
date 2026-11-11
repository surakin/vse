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
- **Reals are numbers, not bit patterns.** A backend may return `-0.0` as `0.0`, and a NaN cannot be bound (`Misuse`); store bits in a blob or an integer when they matter. (Comparison of `ValueView`s is bitwise; the database is not.)
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

## The SQLite backend

`database::SqliteDatabase` (`database/sqlite/sqlite_database.h`) is the backend. An empty `OpenOptions::location` or `":memory:"` is a private in-memory
database that belongs to its connection (two connections never share one, so the cases that need two skip on that fixture); any other location is a file.

- **Files go through the file layer.** SQLite's virtual file system (`engine/database/sqlite/sqlite_vfs.cpp`) opens, reads, writes, syncs, truncates and
  deletes only through `io::File` and `io::Path`; temporary files, the clock (`os::WallClockUnixMillis`) and randomness (`os::FillRandomBytes`) come from the
  engine as well. A new platform needs no SQLite port.
- **Write-ahead log by default.** A file opens in `WAL` mode, so readers do not block the writer (`Supports(ConcurrentReaders)`); `JournalMode::Delete` and
  `Truncate` select a rollback journal. `sync_mode` `Default` is `Normal`; `Full` syncs on every commit.
- **One process per file.** The file locks and the write-ahead log's index are kept in this process, shared by its connections, with SQLite's real rules. A
  second process is not excluded: do not open one database file from two programs at once (`KNOWN-ISSUES.md`).
- **Crash safety is tested**, not assumed: `tests/database/sqlite_crash_test.cpp` fails the Nth write, sync or truncation of a 200-row transaction, or stops
  all file operations there as a crash would, for every N in the rollback-journal and write-ahead-log modes; the file always reopens, passes SQLite's own
  integrity check, and holds either the old rows or all the new ones.
- A savepoint outside a transaction starts one, as in SQLite; releasing the outermost savepoint commits it.
- `SchemaVersion` is `PRAGMA user_version`, inside the backend only.
- A statement used after its connection was destroyed fails with `Misuse`; the connection's memory lives until the last statement is gone.
- SQLite reports a syntax error and an unknown table both as one code; the backend tells them apart by the message (`Syntax`, `NotFound`).
- A file that is not a database is `Corrupt` at open; a missing file without `OpenMode::Create` is `NotFound`.

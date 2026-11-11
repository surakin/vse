#include "storage/sql_store.h"

namespace engine_placeholder::storage {

namespace {

using database::Connection;
using database::Statement;
using database::ValueView;

// The error a database failure becomes. The database's own message stays in its Status; the store's is a fixed phrase per code.
Status Map(const database::Status& s) {
    switch (s.Code()) {
    case database::ErrorCode::Ok:
        return Status::Success();
    case database::ErrorCode::Busy:
    case database::ErrorCode::Locked:
        return Status::Make(ErrorCode::Busy, "the database is busy");
    case database::ErrorCode::Corrupt:
        return Status::Make(ErrorCode::Corrupt, "the database is damaged");
    case database::ErrorCode::TooBig:
        return Status::Make(ErrorCode::LimitExceeded, "a value is larger than the database allows");
    case database::ErrorCode::Full:
    case database::ErrorCode::Io:
    case database::ErrorCode::ReadOnly:
        return Status::Make(ErrorCode::Io, "the database could not be read or written");
    default:
        return Status::Make(ErrorCode::Other, "the database reported an error");
    }
}

// Runs `expr` (a database status) and returns the mapped failure.
#define SQL_TRY(expr)                                 \
    do {                                              \
        const database::Status sql_try_status = (expr); \
        if (sql_try_status.Failed()) {                \
            return Map(sql_try_status);               \
        }                                             \
    } while (false)

constexpr usize kKeyBytes = 16;
constexpr usize kHashBytes = 8;

void PutBigEndian(u64 v, byte* out) {
    for (usize i = 0; i < 8; ++i) {
        out[i] = static_cast<byte>((v >> (56 - 8 * i)) & 0xFF);
    }
}

u64 GetBigEndian(const byte* in) {
    u64 v = 0;
    for (usize i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<u64>(in[i]);
    }
    return v;
}

struct KeyBytes {
    byte b[kKeyBytes];
    explicit KeyBytes(EntityKey key) {
        PutBigEndian(key.high, b);
        PutBigEndian(key.low, b + 8);
    }
    ValueView View() const { return ValueView::Blob(ByteSpan(b, kKeyBytes)); }
};

struct HashBytesBE {
    byte b[kHashBytes];
    explicit HashBytesBE(u64 hash) { PutBigEndian(hash, b); }
    ValueView View() const { return ValueView::Blob(ByteSpan(b, kHashBytes)); }
};

// Reads (key, hash, data) from the current row of the scan and get statements into `out`; false when a column has the wrong shape.
bool ReadRow(const Statement& st, u32 first, bool with_key, EntityRecord* out) {
    u32 c = first;
    if (with_key) {
        const ValueView k = st.Column(c++);
        if (k.Type() != database::ValueType::Blob || k.AsBlob().Size() != kKeyBytes) {
            return false;
        }
        out->key = EntityKey{GetBigEndian(k.AsBlob().Data()), GetBigEndian(k.AsBlob().Data() + 8)};
    }
    const ValueView h = st.Column(c++);
    const ValueView d = st.Column(c);
    if (h.Type() != database::ValueType::Blob || h.AsBlob().Size() != kHashBytes || d.Type() != database::ValueType::Blob) {
        return false;
    }
    out->hash = GetBigEndian(h.AsBlob().Data());
    out->blob.Clear();
    out->blob.Reserve(d.AsBlob().Size());
    for (usize i = 0; i < d.AsBlob().Size(); ++i) {
        out->blob.Push(d.AsBlob()[i]);
    }
    return out->HashMatches();
}

// --- schema and its migrations: step n takes the database from version n-1 to n ---

database::Status StepOne(Connection& c) {
    // The first layout: records without the number of the write.
    database::Status s = c.Execute("CREATE TABLE meta (key TEXT NOT NULL PRIMARY KEY, value BLOB NOT NULL)");
    if (s.Failed()) {
        return s;
    }
    return c.Execute("CREATE TABLE entity (key BLOB NOT NULL PRIMARY KEY, hash BLOB NOT NULL, data BLOB NOT NULL)");
}

// The second layout adds `seq`. A backend that can add a column does it in place (the old rows get 0); otherwise the rows go through a scratch table,
// because the portable subset has no rename.
database::Status StepTwo(Connection& c) {
    if (c.Supports(database::Feature::AddColumn)) {
        database::Status added = c.Execute("ALTER TABLE entity ADD COLUMN seq INTEGER");
        if (added.Failed()) {
            return added;
        }
        return c.Execute("UPDATE entity SET seq = 0");
    }
    database::Status s = c.Execute("CREATE TABLE entity_scratch (key BLOB NOT NULL PRIMARY KEY, hash BLOB NOT NULL, data BLOB NOT NULL)");
    if (s.Failed()) {
        return s;
    }
    {
        UniquePtr<Statement> from, to;
        s = c.Prepare("SELECT key, hash, data FROM entity ORDER BY key ASC", &from);
        if (s.Failed()) {
            return s;
        }
        s = c.Prepare("INSERT INTO entity_scratch (key, hash, data) VALUES (?, ?, ?)", &to);
        if (s.Failed()) {
            return s;
        }
        bool row = false;
        for (s = from->Step(&row); s.Ok() && row; s = from->Step(&row)) {
            for (u32 i = 0; i < 3; ++i) {
                s = to->Bind(i + 1, from->Column(i));
                if (s.Failed()) {
                    return s;
                }
            }
            bool unused = false;
            s = to->Step(&unused);
            if (s.Failed()) {
                return s;
            }
            s = to->Reset();
            if (s.Failed()) {
                return s;
            }
        }
        if (s.Failed()) {
            return s;
        }
    }
    s = c.Execute("DROP TABLE entity");
    if (s.Failed()) {
        return s;
    }
    s = c.Execute("CREATE TABLE entity (key BLOB NOT NULL PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)");
    if (s.Failed()) {
        return s;
    }
    {
        UniquePtr<Statement> from, to;
        s = c.Prepare("SELECT key, hash, data FROM entity_scratch ORDER BY key ASC", &from);
        if (s.Failed()) {
            return s;
        }
        s = c.Prepare("INSERT INTO entity (key, seq, hash, data) VALUES (?, ?, ?, ?)", &to);
        if (s.Failed()) {
            return s;
        }
        bool row = false;
        i64 seq = 1;
        for (s = from->Step(&row); s.Ok() && row; s = from->Step(&row)) {
            s = to->Bind(1, from->Column(0));
            if (s.Ok()) {
                s = to->Bind(2, ValueView::Integer(seq++));
            }
            for (u32 i = 1; i < 3 && s.Ok(); ++i) {
                s = to->Bind(i + 2, from->Column(i));
            }
            if (s.Failed()) {
                return s;
            }
            bool unused = false;
            s = to->Step(&unused);
            if (s.Failed()) {
                return s;
            }
            s = to->Reset();
            if (s.Failed()) {
                return s;
            }
        }
        if (s.Failed()) {
            return s;
        }
    }
    return c.Execute("DROP TABLE entity_scratch");
}

using MigrationStep = database::Status (*)(Connection&);
constexpr MigrationStep kMigrations[] = {&StepOne, &StepTwo};
static_assert(sizeof(kMigrations) / sizeof(kMigrations[0]) == static_cast<usize>(SqlStore::kSchemaVersion), "one migration step per schema version");

} // namespace

struct SqlStore::Statements {
    UniquePtr<Statement> put;
    UniquePtr<Statement> get;
    UniquePtr<Statement> remove;
    UniquePtr<Statement> scan_from;
    UniquePtr<Statement> scan_range;
    UniquePtr<Statement> count;
    UniquePtr<Statement> meta_get;
    UniquePtr<Statement> meta_put;
};

SqlStore::SqlStore(Connection& connection) : connection_(&connection), statements_(MakeUnique<Statements>()) {}

SqlStore::~SqlStore() {
    if (in_batch_) {
        static_cast<void>(connection_->Rollback()); // nothing to report from a destructor: the batch was never committed
    }
}

Status SqlStore::Open(Connection& connection, UniquePtr<SqlStore>* out) {
    if (connection.InTransaction()) {
        return Status::Make(ErrorCode::Invalid, "the connection is inside a transaction");
    }
    {
        database::Transaction transaction(connection, database::TransactionKind::Immediate);
        SQL_TRY(transaction.BeginStatus());
        i64 version = 0;
        SQL_TRY(connection.SchemaVersion(&version));
        if (version > kSchemaVersion) {
            return Status::Make(ErrorCode::TooNew, "the database has a newer schema than this program knows"); // the guard rolls back; nothing was written
        }
        if (version < 0) {
            return Status::Make(ErrorCode::Corrupt, "the schema version is negative");
        }
        for (i64 v = version; v < kSchemaVersion; ++v) {
            SQL_TRY(kMigrations[v](connection));
            SQL_TRY(connection.WriteSchemaVersion(v + 1));
        }
        if (version < kSchemaVersion) {
            UniquePtr<Statement> put;
            SQL_TRY(connection.Prepare("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", &put));
            const char* text = "2";
            SQL_TRY(put->Bind(1, ValueView::Text(metadata::kSchemaVersion)));
            SQL_TRY(put->Bind(2, ValueView::Blob(ByteSpan(reinterpret_cast<const byte*>(text), 1))));
            bool unused = false;
            SQL_TRY(put->Step(&unused));
        }
        SQL_TRY(transaction.Commit());
    }
    UniquePtr<SqlStore> store = MakeUnique<SqlStore>(connection);
    if (!store) {
        return Status::Make(ErrorCode::Other, "out of memory");
    }
    ENGINE_TRY(store->Prepare());
    *out = Move(store);
    return Status::Success();
}

Status SqlStore::Prepare() {
    Connection& c = *connection_;
    Statements& s = *statements_;
    SQL_TRY(c.Prepare("INSERT OR REPLACE INTO entity (key, seq, hash, data) VALUES (?, ?, ?, ?)", &s.put));
    SQL_TRY(c.Prepare("SELECT hash, data FROM entity WHERE key = ?", &s.get));
    SQL_TRY(c.Prepare("DELETE FROM entity WHERE key = ?", &s.remove));
    SQL_TRY(c.Prepare("SELECT key, hash, data FROM entity WHERE key >= ? ORDER BY key ASC", &s.scan_from));
    SQL_TRY(c.Prepare("SELECT key, hash, data FROM entity WHERE key >= ? AND key < ? ORDER BY key ASC", &s.scan_range));
    SQL_TRY(c.Prepare("SELECT count(*) FROM entity", &s.count));
    SQL_TRY(c.Prepare("SELECT value FROM meta WHERE key = ?", &s.meta_get));
    SQL_TRY(c.Prepare("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", &s.meta_put));
    // The next write number: after the largest stored.
    UniquePtr<Statement> top;
    SQL_TRY(c.Prepare("SELECT seq FROM entity ORDER BY seq DESC LIMIT 1", &top));
    bool row = false;
    SQL_TRY(top->Step(&row));
    if (row && top->Column(0).Type() == database::ValueType::Integer) {
        next_seq_ = top->Column(0).AsInteger() + 1;
    }
    return Status::Success();
}

const char* SqlStore::Name() const {
    return "sql";
}

void SqlStore::FailAfter(u32 n) {
    LockGuard<Mutex> guard(lock_);
    fail_in_ = n;
}

bool SqlStore::ShouldFail() {
    if (fail_in_ == 0) {
        return false;
    }
    --fail_in_;
    return fail_in_ == 0;
}

Status SqlStore::Guard() const {
    if (in_batch_ && owner_ != ThisThread::Id()) {
        return Status::Make(ErrorCode::Busy, "a batch belongs to another thread");
    }
    return Status::Success();
}

Status SqlStore::Put(const EntityRecord& record) {
    if (record.key.IsNull()) {
        return Status::Make(ErrorCode::Invalid, "a record needs a key");
    }
    if (!record.HashMatches()) {
        return Status::Make(ErrorCode::Invalid, "the record's hash is not the hash of its bytes");
    }
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    if (ShouldFail()) {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    Statement& st = *statements_->put;
    const KeyBytes key(record.key);
    const HashBytesBE hash(record.hash);
    SQL_TRY(st.Bind(1, key.View()));
    SQL_TRY(st.Bind(2, ValueView::Integer(next_seq_)));
    SQL_TRY(st.Bind(3, hash.View()));
    SQL_TRY(st.Bind(4, ValueView::Blob(ByteSpan(record.blob.Data(), record.blob.Size()))));
    bool unused = false;
    const database::Status stepped = st.Step(&unused);
    static_cast<void>(st.Reset()); // the step's own failure is the one reported
    SQL_TRY(stepped);
    ++next_seq_;
    return Status::Success();
}

Status SqlStore::Get(EntityKey key, EntityRecord* out) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    Statement& st = *statements_->get;
    const KeyBytes bytes(key);
    SQL_TRY(st.Bind(1, bytes.View()));
    bool row = false;
    const database::Status stepped = st.Step(&row);
    Status result = Status::Success();
    if (stepped.Failed()) {
        result = Map(stepped);
    } else if (!row) {
        result = Status::Make(ErrorCode::NotFound, "no record with this key");
    } else {
        EntityRecord found;
        found.key = key;
        if (ReadRow(st, 0, false, &found)) {
            *out = Move(found);
        } else {
            result = Status::Make(ErrorCode::Corrupt, "the stored record does not match its hash");
        }
    }
    static_cast<void>(st.Reset());
    return result;
}

Status SqlStore::Remove(EntityKey key) {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    if (ShouldFail()) {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    Statement& st = *statements_->remove;
    const KeyBytes bytes(key);
    SQL_TRY(st.Bind(1, bytes.View()));
    bool unused = false;
    const database::Status stepped = st.Step(&unused);
    static_cast<void>(st.Reset());
    SQL_TRY(stepped);
    if (connection_->ChangedRows() == 0) {
        return Status::Make(ErrorCode::NotFound, "no record with this key");
    }
    return Status::Success();
}

Status SqlStore::Scan(const KeyRange& range, RecordVisitor& visitor) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    Statement& st = range.unbounded ? *statements_->scan_from : *statements_->scan_range;
    const KeyBytes first(range.first);
    const KeyBytes end(range.end);
    SQL_TRY(st.Bind(1, first.View()));
    if (!range.unbounded) {
        SQL_TRY(st.Bind(2, end.View()));
    }
    Status result = Status::Success();
    bool row = false;
    database::Status stepped = st.Step(&row);
    while (stepped.Ok() && row) {
        EntityRecord record;
        if (!ReadRow(st, 0, true, &record)) {
            result = Status::Make(ErrorCode::Corrupt, "a stored record does not match its hash");
            break;
        }
        if (!visitor.Visit(record)) {
            break;
        }
        stepped = st.Step(&row);
    }
    if (stepped.Failed()) {
        result = Map(stepped);
    }
    static_cast<void>(st.Reset());
    return result;
}

Status SqlStore::Count(usize* out) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    Statement& st = *statements_->count;
    bool row = false;
    const database::Status stepped = st.Step(&row);
    Status result = Map(stepped);
    if (result.Ok()) {
        if (row && st.Column(0).Type() == database::ValueType::Integer) {
            *out = static_cast<usize>(st.Column(0).AsInteger());
        } else {
            result = Status::Make(ErrorCode::Corrupt, "the record count is not a number");
        }
    }
    static_cast<void>(st.Reset());
    return result;
}

Status SqlStore::Begin() {
    LockGuard<Mutex> guard(lock_);
    if (in_batch_) {
        return Status::Make(ErrorCode::Busy, "a batch is open already");
    }
    if (ShouldFail()) {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    SQL_TRY(connection_->Begin(database::TransactionKind::Immediate));
    in_batch_ = true;
    owner_ = ThisThread::Id();
    return Status::Success();
}

Status SqlStore::Commit() {
    LockGuard<Mutex> guard(lock_);
    if (!in_batch_ || owner_ != ThisThread::Id()) {
        return Status::Make(ErrorCode::Invalid, "this thread has no open batch");
    }
    if (ShouldFail()) {
        return Status::Make(ErrorCode::Io, "an injected failure"); // the batch stays open
    }
    SQL_TRY(connection_->Commit());
    in_batch_ = false;
    return Status::Success();
}

Status SqlStore::Rollback() {
    LockGuard<Mutex> guard(lock_);
    if (!in_batch_ || owner_ != ThisThread::Id()) {
        return Status::Make(ErrorCode::Invalid, "this thread has no open batch");
    }
    in_batch_ = false;
    SQL_TRY(connection_->Rollback());
    return Status::Success();
}

Status SqlStore::GetMetadata(StringView name, String<HeapAllocator>* out) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    Statement& st = *statements_->meta_get;
    SQL_TRY(st.Bind(1, ValueView::Text(name)));
    bool row = false;
    const database::Status stepped = st.Step(&row);
    Status result = Map(stepped);
    if (result.Ok()) {
        if (!row) {
            result = Status::Make(ErrorCode::NotFound, "no such metadata");
        } else if (st.Column(0).Type() != database::ValueType::Blob) {
            result = Status::Make(ErrorCode::Corrupt, "a metadata value is not a blob");
        } else {
            const ByteSpan value = st.Column(0).AsBlob();
            String<HeapAllocator> text(HeapAllocator::Default());
            text.Append(StringView(reinterpret_cast<const char*>(value.Data()), value.Size()));
            *out = Move(text);
        }
    }
    static_cast<void>(st.Reset());
    return result;
}

Status SqlStore::SetMetadata(StringView name, StringView value) {
    if (name.Length() == 0) {
        return Status::Make(ErrorCode::Invalid, "a metadata name cannot be empty");
    }
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    if (ShouldFail()) {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    Statement& st = *statements_->meta_put;
    SQL_TRY(st.Bind(1, ValueView::Text(name)));
    SQL_TRY(st.Bind(2, ValueView::Blob(ByteSpan(reinterpret_cast<const byte*>(value.Data()), value.Length()))));
    bool unused = false;
    const database::Status stepped = st.Step(&unused);
    static_cast<void>(st.Reset());
    return Map(stepped);
}

} // namespace engine_placeholder::storage

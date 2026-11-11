#include "storage/sql_store.h"

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "ecs/component.h"
#include "storage/component_record.h"

namespace engine_placeholder::storage {

namespace detail {
Status MapDatabaseStatus(const database::Status& s) {
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
} // namespace detail

namespace {

using database::Connection;
using database::Statement;
using database::ValueView;

Status Map(const database::Status& s) {
    return detail::MapDatabaseStatus(s);
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
    byte b[kKeyBytes] = {};
    KeyBytes() = default;
    explicit KeyBytes(EntityKey key) { Set(key); }
    void Set(EntityKey key) {
        PutBigEndian(key.high, b);
        PutBigEndian(key.low, b + 8);
    }
    ValueView View() const { return ValueView::Blob(ByteSpan(b, kKeyBytes)); }
};

struct HashBytesBE {
    byte b[kHashBytes] = {};
    HashBytesBE() = default;
    explicit HashBytesBE(u64 hash) { Set(hash); }
    void Set(u64 hash) { PutBigEndian(hash, b); }
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

// The third layout is the second as a table without a row id where the backend has one (the primary key is the row's address, so no second index is kept).
// A backend without it keeps the table as it is.
database::Status StepThree(Connection& c) {
    if (!c.Supports(database::Feature::WithoutRowId)) {
        return database::Status::Success();
    }
    database::Status s = c.Execute("CREATE TABLE entity_scratch (key BLOB NOT NULL PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)");
    if (s.Failed()) {
        return s;
    }
    {
        UniquePtr<Statement> from, to;
        s = c.Prepare("SELECT key, seq, hash, data FROM entity ORDER BY key ASC", &from);
        if (s.Ok()) {
            s = c.Prepare("INSERT INTO entity_scratch (key, seq, hash, data) VALUES (?, ?, ?, ?)", &to);
        }
        bool row = false;
        for (s = s.Ok() ? from->Step(&row) : s; s.Ok() && row; s = from->Step(&row)) {
            for (u32 i = 0; i < 4 && s.Ok(); ++i) {
                s = to->Bind(i + 1, from->Column(i));
            }
            bool unused = false;
            if (s.Ok()) {
                s = to->Step(&unused);
            }
            if (s.Ok()) {
                s = to->Reset();
            }
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
    s = c.Execute("CREATE TABLE entity (key BLOB NOT NULL PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL) WITHOUT ROWID");
    if (s.Failed()) {
        return s;
    }
    {
        UniquePtr<Statement> from, to;
        s = c.Prepare("SELECT key, seq, hash, data FROM entity_scratch ORDER BY key ASC", &from);
        if (s.Ok()) {
            s = c.Prepare("INSERT INTO entity (key, seq, hash, data) VALUES (?, ?, ?, ?)", &to);
        }
        bool row = false;
        for (s = s.Ok() ? from->Step(&row) : s; s.Ok() && row; s = from->Step(&row)) {
            for (u32 i = 0; i < 4 && s.Ok(); ++i) {
                s = to->Bind(i + 1, from->Column(i));
            }
            bool unused = false;
            if (s.Ok()) {
                s = to->Step(&unused);
            }
            if (s.Ok()) {
                s = to->Reset();
            }
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
constexpr MigrationStep kMigrations[] = {&StepOne, &StepTwo, &StepThree};
static_assert(sizeof(kMigrations) / sizeof(kMigrations[0]) == static_cast<usize>(SqlStore::kSchemaVersion), "one migration step per schema version");

} // namespace

struct SqlStore::Statements {
    UniquePtr<Statement> put;
    UniquePtr<Statement> put_many;
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
    return Open(connection, out, SqlStoreOptions());
}

Status SqlStore::Open(Connection& connection, UniquePtr<SqlStore>* out, const SqlStoreOptions& options) {
    if (options.columns && options.registry == nullptr) {
        return Status::Make(ErrorCode::Invalid, "the column layout needs a component registry");
    }
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
            const byte digit = static_cast<byte>('0' + kSchemaVersion); // one digit: the versions are few
            SQL_TRY(put->Bind(1, ValueView::Text(metadata::kSchemaVersion)));
            SQL_TRY(put->Bind(2, ValueView::Blob(ByteSpan(&digit, 1))));
            bool unused = false;
            SQL_TRY(put->Step(&unused));
        }
        SQL_TRY(transaction.Commit());
    }
    UniquePtr<SqlStore> store = MakeUnique<SqlStore>(connection);
    if (!store) {
        return Status::Make(ErrorCode::Other, "out of memory");
    }
    store->limits_ = options.limits;
    store->registry_ = options.registry;
    ENGINE_TRY(store->Prepare());
    ENGINE_TRY(store->SetupLayout(options));
    *out = Move(store);
    return Status::Success();
}

Status SqlStore::Prepare() {
    Connection& c = *connection_;
    Statements& s = *statements_;
    SQL_TRY(c.Prepare("INSERT OR REPLACE INTO entity (key, seq, hash, data) VALUES (?, ?, ?, ?)", &s.put));
    SQL_TRY(c.Prepare(
        "INSERT OR REPLACE INTO entity (key, seq, hash, data) VALUES (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), "
        "(?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?), (?, ?, ?, ?)",
        &s.put_many));
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


// --- the column layout ---

namespace detail {

struct SqlTable {
    struct Column {
        String<HeapAllocator> name{HeapAllocator::Default()};
        bool real = false;
        u32 field = 0;
        u32 part = 0;
    };
    const RegisteredComponent* component = nullptr;
    String<HeapAllocator> name{HeapAllocator::Default()};      // c_<component>
    Vector<Column> columns;
    String<HeapAllocator> signature{HeapAllocator::Default()}; // "a:I,b_x:R,..."
    UniquePtr<database::Statement> put;
    UniquePtr<database::Statement> del;
    UniquePtr<database::Statement> select;
    bool needs_fill = false;
};

struct SqlLayout {
    Vector<UniquePtr<SqlTable>> tables;
};

} // namespace detail

namespace {

using detail::SqlLayout;
using detail::SqlTable;

struct Cell {
    bool real = false;
    bool null = false; // a real that is NaN
    i64 integer = 0;
    f64 number = 0;
};

bool IsIdentifier(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (usize i = 0; name[i] != '\0'; ++i) {
        const char c = name[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!(letter || (digit && i != 0))) {
            return false;
        }
    }
    return true;
}

u64 ReadLittle(const byte* p, usize n) {
    u64 v = 0;
    for (usize i = 0; i < n; ++i) {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

i64 SignExtend(u64 v, usize bytes) {
    if (bytes >= 8) {
        return static_cast<i64>(v);
    }
    const u64 sign = u64(1) << (bytes * 8 - 1);
    return static_cast<i64>((v ^ sign) - sign);
}

f64 FloatAt(const byte* p) {
    const u32 bits = static_cast<u32>(ReadLittle(p, 4));
    f32 f = 0;
    MemCopy(&f, &bits, 4);
    return static_cast<f64>(f);
}

f64 DoubleAt(const byte* p) {
    const u64 bits = ReadLittle(p, 8);
    f64 f = 0;
    MemCopy(&f, &bits, 8);
    return f;
}

// How many columns a field of this kind makes: scalars one, vectors one per part, the rest none.
u32 PartsOf(FieldKind kind) {
    switch (kind) {
    case FieldKind::U8: case FieldKind::U16: case FieldKind::U32: case FieldKind::U64:
    case FieldKind::I8: case FieldKind::I16: case FieldKind::I32: case FieldKind::I64:
    case FieldKind::F32: case FieldKind::F64: case FieldKind::Bool: case FieldKind::Enum:
        return 1;
    case FieldKind::Vec2:
        return 2;
    case FieldKind::Vec3:
        return 3;
    case FieldKind::Vec4: case FieldKind::Quat:
        return 4;
    default:
        return 0;
    }
}

bool IsRealKind(FieldKind kind) {
    return kind == FieldKind::F32 || kind == FieldKind::F64 || PartsOf(kind) > 1;
}

String<HeapAllocator> NewText() {
    return String<HeapAllocator>(HeapAllocator::Default());
}

void Add(String<HeapAllocator>& to, const char* text) {
    to.Append(StringView(text));
}

database::SqlText Raw(const String<HeapAllocator>& text) {
    return database::SqlText::Unchecked(text.View());
}

// The columns of a queryable component, in field order.
Status BuildColumns(const RegisteredComponent& component, SqlTable* table) {
    static const char* const kParts[] = {"_x", "_y", "_z", "_w"};
    const Span<const FieldDesc> fields = component.Fields();
    for (usize f = 0; f < fields.Size(); ++f) {
        const u32 parts = PartsOf(fields[f].kind);
        if (parts == 0) {
            continue;
        }
        if (!IsIdentifier(fields[f].name)) {
            return Status::Make(ErrorCode::Invalid, "a queryable field name is not a plain identifier");
        }
        for (u32 part = 0; part < parts; ++part) {
            SqlTable::Column column;
            column.name = NewText();
            Add(column.name, fields[f].name);
            if (parts > 1) {
                Add(column.name, kParts[part]);
            }
            column.real = IsRealKind(fields[f].kind);
            column.field = static_cast<u32>(f);
            column.part = part;
            table->columns.Push(Move(column));
        }
    }
    table->signature = NewText();
    for (usize i = 0; i < table->columns.Size(); ++i) {
        if (i != 0) {
            Add(table->signature, ",");
        }
        table->signature.Append(table->columns[i].name.View());
        Add(table->signature, table->columns[i].real ? ":R" : ":I");
    }
    return Status::Success();
}

// The values of one entity's component, from its blob. `found` is false when the entity does not have the component.
Status ExtractCells(const SqlTable& table, ByteSpan blob, const CodecLimits& limits, bool* found, Vector<Cell>* cells) {
    *found = false;
    bytes::ByteReader in(blob);
    u32 count = 0;
    if (in.ReadU32(&count) != bytes::ByteError::Ok) {
        return Status::Make(ErrorCode::Corrupt, "a stored record is truncated");
    }
    const RegisteredComponent& component = *table.component;
    for (u32 i = 0; i < count; ++i) {
        ComponentRecord record;
        ENGINE_TRY(ReadComponentRecord(in, limits, &record));
        if (record.id != component.Id()) {
            continue;
        }
        const ecs::TypeInfo& info = ecs::ComponentRegistry::Global().Info(component.EcsId());
        if (info.default_construct == nullptr) {
            return Status::Make(ErrorCode::Invalid, "a queryable component needs a default constructor");
        }
        void* object = HeapAllocator::Default().Alloc(info.size, info.align);
        if (object == nullptr) {
            return Status::Make(ErrorCode::Other, "out of memory");
        }
        info.default_construct(object);
        Status result = component.DecodeRecord(record.version, record.bytes, object, limits);
        const Span<const FieldDesc> fields = component.Fields();
        u32 current_field = ~u32(0);
        bytes::ByteWriter encoded;
        for (usize c = 0; c < table.columns.Size() && result.Ok(); ++c) {
            const SqlTable::Column& column = table.columns[c];
            const FieldDesc& desc = fields[column.field];
            if (column.field != current_field) {
                current_field = column.field;
                encoded.Clear();
                result = desc.encode(desc, object, encoded, limits);
                if (result.Failed()) {
                    break;
                }
            }
            const byte* p = encoded.Bytes().Data();
            Cell cell;
            cell.real = column.real;
            switch (desc.kind) {
            case FieldKind::U8: case FieldKind::Bool: cell.integer = static_cast<i64>(ReadLittle(p, 1)); break;
            case FieldKind::U16: cell.integer = static_cast<i64>(ReadLittle(p, 2)); break;
            case FieldKind::U32: cell.integer = static_cast<i64>(ReadLittle(p, 4)); break;
            case FieldKind::U64: cell.integer = static_cast<i64>(ReadLittle(p, 8)); break;
            case FieldKind::I8: cell.integer = SignExtend(ReadLittle(p, 1), 1); break;
            case FieldKind::I16: cell.integer = SignExtend(ReadLittle(p, 2), 2); break;
            case FieldKind::I32: cell.integer = SignExtend(ReadLittle(p, 4), 4); break;
            case FieldKind::I64: cell.integer = static_cast<i64>(ReadLittle(p, 8)); break;
            case FieldKind::Enum: {
                const usize n = desc.memory_size;
                const u64 raw = ReadLittle(p, n);
                cell.integer = desc.options.range_min < 0 ? SignExtend(raw, n) : static_cast<i64>(raw);
                break;
            }
            case FieldKind::F32: cell.number = FloatAt(p); break;
            case FieldKind::F64: cell.number = DoubleAt(p); break;
            default: cell.number = FloatAt(p + 4 * column.part); break; // a vector part
            }
            if (cell.real && !(cell.number == cell.number)) {
                cell.null = true;
            }
            cells->Push(cell);
        }
        info.destroy(object);
        HeapAllocator::Default().Free(object, info.size, info.align);
        ENGINE_TRY(result);
        *found = true;
        return Status::Success();
    }
    return Status::Success();
}

database::ValueView ToValue(const Cell& cell) {
    if (!cell.real) {
        return ValueView::Integer(cell.integer);
    }
    return cell.null ? ValueView::Null() : ValueView::Real(cell.number);
}

String<HeapAllocator> CreateText(const SqlTable& table, bool without_rowid) {
    String<HeapAllocator> sql = NewText();
    Add(sql, "CREATE TABLE IF NOT EXISTS ");
    sql.Append(table.name.View());
    Add(sql, " (key BLOB NOT NULL PRIMARY KEY REFERENCES entity(key) ON DELETE CASCADE");
    for (usize i = 0; i < table.columns.Size(); ++i) {
        Add(sql, ", ");
        sql.Append(table.columns[i].name.View());
        Add(sql, table.columns[i].real ? " REAL" : " INTEGER NOT NULL");
    }
    Add(sql, without_rowid ? ") WITHOUT ROWID" : ")");
    return sql;
}

} // namespace

Status SqlStore::SetupLayout(const SqlStoreOptions& options) {
    Connection& c = *connection_;
    database::Transaction tx(c, database::TransactionKind::Immediate);
    SQL_TRY(tx.BeginStatus());
    UniquePtr<Statement> meta_get, meta_put;
    SQL_TRY(c.Prepare("SELECT value FROM meta WHERE key = ?", &meta_get));
    SQL_TRY(c.Prepare("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", &meta_put));
    const auto read_meta = [&](StringView name, String<HeapAllocator>* value, bool* present) -> database::Status {
        *present = false;
        database::Status s = meta_get->Bind(1, ValueView::Text(name));
        bool row = false;
        if (s.Ok()) {
            s = meta_get->Step(&row);
        }
        if (s.Ok() && row && meta_get->Column(0).Type() == database::ValueType::Blob) {
            const ByteSpan b = meta_get->Column(0).AsBlob();
            *value = NewText();
            value->Append(StringView(reinterpret_cast<const char*>(b.Data()), b.Size()));
            *present = true;
        }
        static_cast<void>(meta_get->Reset());
        return s;
    };
    const auto write_meta = [&](StringView name, StringView value) -> database::Status {
        database::Status s = meta_put->Bind(1, ValueView::Text(name));
        if (s.Ok()) {
            s = meta_put->Bind(2, ValueView::Blob(ByteSpan(reinterpret_cast<const byte*>(value.Data()), value.Length())));
        }
        bool unused = false;
        if (s.Ok()) {
            s = meta_put->Step(&unused);
        }
        static_cast<void>(meta_put->Reset());
        return s;
    };

    String<HeapAllocator> active = NewText();
    bool has_active = false;
    SQL_TRY(read_meta("layout.active", &active, &has_active));
    const bool was_active = has_active && active.View() == StringView("1");
    if (!options.columns) {
        if (was_active) {
            SQL_TRY(write_meta("layout.active", "0")); // the column tables are stale from now on: the next columns open refills them
        }
        SQL_TRY(tx.Commit());
        return Status::Success();
    }

    UniquePtr<SqlLayout> layout = MakeUnique<SqlLayout>();
    const ComponentRegistry& registry = *options.registry;
    for (u32 i = 0; i < registry.Count(); ++i) {
        const RegisteredComponent& component = registry.At(i);
        if (!component.IsQueryable() || component.IsTag() || component.EcsId() == ecs::kInvalidComponent) {
            continue;
        }
        if (!IsIdentifier(component.Name())) {
            return Status::Make(ErrorCode::Invalid, "a queryable component name is not a plain identifier");
        }
        UniquePtr<SqlTable> table = MakeUnique<SqlTable>();
        table->component = &component;
        table->name = NewText();
        Add(table->name, "c_");
        Add(table->name, component.Name());
        ENGINE_TRY(BuildColumns(component, table.Get()));
        if (table->columns.Empty()) {
            continue; // nothing in it to query
        }
        layout->tables.Push(Move(table));
    }

    const bool without_rowid = c.Supports(database::Feature::WithoutRowId);
    bool any_fill = false;
    for (usize t = 0; t < layout->tables.Size(); ++t) {
        SqlTable& table = *layout->tables[t];
        String<HeapAllocator> meta_name = NewText();
        Add(meta_name, "layout.");
        meta_name.Append(table.name.View());
        String<HeapAllocator> old_signature = NewText();
        bool has_old = false;
        SQL_TRY(read_meta(meta_name.View(), &old_signature, &has_old));
        if (!has_old) {
            SQL_TRY(c.Execute(Raw(CreateText(table, without_rowid))));
            table.needs_fill = true;
        } else if (old_signature.View() != table.signature.View()) {
            const StringView before = old_signature.View();
            const StringView now = table.signature.View();
            const bool appended = now.Length() > before.Length() && now.StartsWith(before) && now[before.Length()] == ',';
            if (appended && c.Supports(database::Feature::AddColumn)) {
                // Only new fields at the end: add their columns (nullable: the old rows have no value until the refill below).
                usize first_new = 1; // the old signature's columns: one more than its commas
                for (usize i = 0; i < before.Length(); ++i) {
                    first_new += before[i] == ',' ? 1 : 0;
                }
                for (usize i = first_new; i < table.columns.Size(); ++i) {
                    String<HeapAllocator> alter = NewText();
                    Add(alter, "ALTER TABLE ");
                    alter.Append(table.name.View());
                    Add(alter, " ADD COLUMN ");
                    alter.Append(table.columns[i].name.View());
                    Add(alter, table.columns[i].real ? " REAL" : " INTEGER");
                    SQL_TRY(c.Execute(Raw(alter)));
                }
            } else {
                String<HeapAllocator> drop = NewText();
                Add(drop, "DROP TABLE ");
                drop.Append(table.name.View());
                SQL_TRY(c.Execute(Raw(drop)));
                SQL_TRY(c.Execute(Raw(CreateText(table, without_rowid))));
            }
            table.needs_fill = true;
        } else if (!was_active) {
            String<HeapAllocator> clear = NewText();
            Add(clear, "DELETE FROM ");
            clear.Append(table.name.View());
            SQL_TRY(c.Execute(Raw(clear)));
            table.needs_fill = true; // rows were written while the layout was off
        }
        any_fill = any_fill || table.needs_fill;

        String<HeapAllocator> put = NewText();
        Add(put, "INSERT OR REPLACE INTO ");
        put.Append(table.name.View());
        Add(put, " (key");
        for (usize i = 0; i < table.columns.Size(); ++i) {
            Add(put, ", ");
            put.Append(table.columns[i].name.View());
        }
        Add(put, ") VALUES (?");
        for (usize i = 0; i < table.columns.Size(); ++i) {
            Add(put, ", ?");
        }
        Add(put, ")");
        SQL_TRY(c.Prepare(Raw(put), &table.put));
        String<HeapAllocator> del = NewText();
        Add(del, "DELETE FROM ");
        del.Append(table.name.View());
        Add(del, " WHERE key = ?");
        SQL_TRY(c.Prepare(Raw(del), &table.del));
        String<HeapAllocator> select = NewText();
        Add(select, "SELECT ");
        for (usize i = 0; i < table.columns.Size(); ++i) {
            if (i != 0) {
                Add(select, ", ");
            }
            select.Append(table.columns[i].name.View());
        }
        Add(select, " FROM ");
        select.Append(table.name.View());
        Add(select, " WHERE key = ?");
        SQL_TRY(c.Prepare(Raw(select), &table.select));
    }
    layout_ = Move(layout);

    for (usize r = 0; r < options.index_count; ++r) {
        const SqlTable* table = nullptr;
        const SqlTable::Column* column = nullptr;
        for (usize t = 0; t < layout_->tables.Size() && table == nullptr; ++t) {
            if (StringView(layout_->tables[t]->component->Name()) == StringView(options.indexes[r].component)) {
                table = layout_->tables[t].Get();
            }
        }
        for (usize i = 0; table != nullptr && i < table->columns.Size(); ++i) {
            if (table->columns[i].name.View() == StringView(options.indexes[r].column)) {
                column = &table->columns[i];
            }
        }
        if (column == nullptr) {
            return Status::Make(ErrorCode::Invalid, "an index names a component or column that is not in the column layout");
        }
        String<HeapAllocator> sql = NewText();
        Add(sql, "CREATE INDEX IF NOT EXISTS i_");
        sql.Append(table->name.View());
        Add(sql, "_");
        sql.Append(column->name.View());
        Add(sql, " ON ");
        sql.Append(table->name.View());
        Add(sql, "(");
        sql.Append(column->name.View());
        Add(sql, ")");
        SQL_TRY(c.Execute(Raw(sql)));
    }

    if (any_fill) {
        // Refill from the blobs: one pass over the entities.
        Statement& scan = *statements_->scan_from;
        const KeyBytes zero;
        SQL_TRY(scan.Bind(1, zero.View()));
        bool row = false;
        database::Status stepped = scan.Step(&row);
        Status result = Status::Success();
        while (stepped.Ok() && row && result.Ok()) {
            EntityRecord record;
            if (!ReadRow(scan, 0, true, &record)) {
                result = Status::Make(ErrorCode::Corrupt, "a stored record does not match its hash");
                break;
            }
            result = WriteColumns(record);
            stepped = scan.Step(&row);
        }
        static_cast<void>(scan.Reset());
        ENGINE_TRY(result);
        SQL_TRY(stepped);
        for (usize t = 0; t < layout_->tables.Size(); ++t) {
            layout_->tables[t]->needs_fill = false;
        }
    }
    for (usize t = 0; t < layout_->tables.Size(); ++t) {
        String<HeapAllocator> meta_name = NewText();
        Add(meta_name, "layout.");
        meta_name.Append(layout_->tables[t]->name.View());
        SQL_TRY(write_meta(meta_name.View(), layout_->tables[t]->signature.View()));
    }
    SQL_TRY(write_meta("layout.active", "1"));
    SQL_TRY(tx.Commit());
    return Status::Success();
}

Status SqlStore::WriteColumns(const EntityRecord& record) const {
    if (layout_ == nullptr) {
        return Status::Success();
    }
    const KeyBytes key(record.key);
    for (usize t = 0; t < layout_->tables.Size(); ++t) {
        SqlTable& table = *layout_->tables[t];
        bool found = false;
        Vector<Cell> cells;
        ENGINE_TRY(ExtractCells(table, ByteSpan(record.blob.Data(), record.blob.Size()), limits_, &found, &cells));
        Statement& st = found ? *table.put : *table.del;
        SQL_TRY(st.Bind(1, key.View()));
        if (found) {
            for (usize i = 0; i < cells.Size(); ++i) {
                SQL_TRY(st.Bind(static_cast<u32>(i + 2), ToValue(cells[i])));
            }
        }
        bool unused = false;
        const database::Status stepped = st.Step(&unused);
        static_cast<void>(st.Reset());
        SQL_TRY(stepped);
    }
    return Status::Success();
}

Status SqlStore::RemoveColumns(EntityKey key) const {
    if (layout_ == nullptr) {
        return Status::Success();
    }
    const KeyBytes bytes(key);
    for (usize t = 0; t < layout_->tables.Size(); ++t) {
        Statement& st = *layout_->tables[t]->del;
        SQL_TRY(st.Bind(1, bytes.View()));
        bool unused = false;
        const database::Status stepped = st.Step(&unused);
        static_cast<void>(st.Reset());
        SQL_TRY(stepped);
    }
    return Status::Success();
}

Status SqlStore::Query(StringView component, StringView column, CompareOp op, const database::ValueView& value, Vector<EntityKey>* keys, usize* count) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    if (layout_ == nullptr) {
        return Status::Make(ErrorCode::Unsupported, "the store has no column layout");
    }
    const SqlTable* table = nullptr;
    for (usize t = 0; t < layout_->tables.Size(); ++t) {
        if (StringView(layout_->tables[t]->component->Name()) == component) {
            table = layout_->tables[t].Get();
            break;
        }
    }
    if (table == nullptr) {
        // A registered component that is not queryable is unsupported; a name nobody registered is not found.
        const bool registered = registry_ != nullptr && registry_->FindByName(component) != nullptr;
        return Status::Make(registered ? ErrorCode::Unsupported : ErrorCode::NotFound, "no queryable component with this name");
    }
    const SqlTable::Column* target = nullptr;
    for (usize i = 0; i < table->columns.Size(); ++i) {
        if (table->columns[i].name.View() == column) {
            target = &table->columns[i];
            break;
        }
    }
    if (target == nullptr) {
        return Status::Make(ErrorCode::NotFound, "the component has no such column");
    }
    ValueView bound = value;
    if (target->real) {
        if (value.Type() == database::ValueType::Integer) {
            bound = ValueView::Real(static_cast<f64>(value.AsInteger()));
        } else if (value.Type() != database::ValueType::Real) {
            return Status::Make(ErrorCode::Invalid, "a real column is compared with a number");
        }
    } else if (value.Type() != database::ValueType::Integer) {
        return Status::Make(ErrorCode::Invalid, "an integer column is compared with an integer");
    }
    String<HeapAllocator> sql = NewText();
    Add(sql, keys != nullptr ? "SELECT key FROM " : "SELECT count(*) FROM ");
    sql.Append(table->name.View());
    Add(sql, " WHERE ");
    sql.Append(target->name.View());
    switch (op) {
    case CompareOp::Equal: Add(sql, " = ?"); break;
    case CompareOp::NotEqual: Add(sql, " <> ?"); break;
    case CompareOp::Less: Add(sql, " < ?"); break;
    case CompareOp::LessOrEqual: Add(sql, " <= ?"); break;
    case CompareOp::Greater: Add(sql, " > ?"); break;
    case CompareOp::GreaterOrEqual: Add(sql, " >= ?"); break;
    }
    if (keys != nullptr) {
        Add(sql, " ORDER BY key ASC");
    }
    UniquePtr<Statement> st;
    SQL_TRY(connection_->Prepare(Raw(sql), &st));
    SQL_TRY(st->Bind(1, bound));
    bool row = false;
    database::Status stepped = st->Step(&row);
    if (keys != nullptr) {
        keys->Clear();
        while (stepped.Ok() && row) {
            const ValueView k = st->Column(0);
            if (k.Type() != database::ValueType::Blob || k.AsBlob().Size() != kKeyBytes) {
                return Status::Make(ErrorCode::Corrupt, "a column table holds a bad key");
            }
            keys->Push(EntityKey{GetBigEndian(k.AsBlob().Data()), GetBigEndian(k.AsBlob().Data() + 8)});
            stepped = st->Step(&row);
        }
        SQL_TRY(stepped);
    } else {
        SQL_TRY(stepped);
        *count = row && st->Column(0).Type() == database::ValueType::Integer ? static_cast<usize>(st->Column(0).AsInteger()) : 0;
    }
    return Status::Success();
}

Status SqlStore::FindKeys(StringView component, StringView column, CompareOp op, const database::ValueView& value, Vector<EntityKey>* out) const {
    return Query(component, column, op, value, out, nullptr);
}

Status SqlStore::CountWhere(StringView component, StringView column, CompareOp op, const database::ValueView& value, usize* out) const {
    return Query(component, column, op, value, nullptr, out);
}

Status SqlStore::VerifyColumns(usize* mismatches) const {
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    if (layout_ == nullptr) {
        return Status::Make(ErrorCode::Unsupported, "the store has no column layout");
    }
    *mismatches = 0;
    for (usize t = 0; t < layout_->tables.Size(); ++t) {
        SqlTable& table = *layout_->tables[t];
        usize with_component = 0;
        Statement& scan = *statements_->scan_from;
        const KeyBytes zero;
        SQL_TRY(scan.Bind(1, zero.View()));
        bool row = false;
        database::Status stepped = scan.Step(&row);
        Status result = Status::Success();
        while (stepped.Ok() && row && result.Ok()) {
            EntityRecord record;
            if (!ReadRow(scan, 0, true, &record)) {
                result = Status::Make(ErrorCode::Corrupt, "a stored record does not match its hash");
                break;
            }
            bool found = false;
            Vector<Cell> cells;
            result = ExtractCells(table, ByteSpan(record.blob.Data(), record.blob.Size()), limits_, &found, &cells);
            if (result.Ok()) {
                const KeyBytes key(record.key);
                Statement& sel = *table.select;
                database::Status s = sel.Bind(1, key.View());
                bool has = false;
                if (s.Ok()) {
                    s = sel.Step(&has);
                }
                if (s.Failed()) {
                    result = Map(s);
                } else if (found != has) {
                    ++*mismatches;
                } else if (found) {
                    ++with_component;
                    for (usize i = 0; i < cells.Size(); ++i) {
                        const ValueView got = sel.Column(static_cast<u32>(i));
                        if (got != ToValue(cells[i])) {
                            ++*mismatches;
                            break;
                        }
                    }
                }
                static_cast<void>(sel.Reset());
            }
            stepped = scan.Step(&row);
        }
        static_cast<void>(scan.Reset());
        ENGINE_TRY(result);
        SQL_TRY(stepped);
        String<HeapAllocator> sql = NewText();
        Add(sql, "SELECT count(*) FROM ");
        sql.Append(table.name.View());
        UniquePtr<Statement> count;
        SQL_TRY(connection_->Prepare(Raw(sql), &count));
        bool has = false;
        SQL_TRY(count->Step(&has));
        const usize rows = has && count->Column(0).Type() == database::ValueType::Integer ? static_cast<usize>(count->Column(0).AsInteger()) : 0;
        if (rows != with_component) {
            *mismatches += rows > with_component ? rows - with_component : with_component - rows; // rows left over for entities that are gone
        }
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
    return WriteColumns(record);
}

Status SqlStore::PutMany(Span<const EntityRecord> records) {
    for (usize i = 0; i < records.Size(); ++i) {
        if (records[i].key.IsNull()) {
            return Status::Make(ErrorCode::Invalid, "a record needs a key");
        }
        if (!records[i].HashMatches()) {
            return Status::Make(ErrorCode::Invalid, "the record's hash is not the hash of its bytes");
        }
    }
    LockGuard<Mutex> guard(lock_);
    ENGINE_TRY(Guard());
    constexpr usize kRows = 16; // the rows of the multi-row statement
    usize i = 0;
    while (i < records.Size()) {
        const usize take = records.Size() - i >= kRows ? kRows : 1;
        for (usize k = 0; k < take; ++k) {
            if (ShouldFail()) {
                return Status::Make(ErrorCode::Io, "an injected failure");
            }
        }
        Statement& st = take == kRows ? *statements_->put_many : *statements_->put;
        KeyBytes keys[kRows];
        HashBytesBE hashes[kRows];
        database::Status bound;
        for (usize k = 0; k < take && bound.Ok(); ++k) {
            const EntityRecord& r = records[i + k];
            keys[k].Set(r.key);
            hashes[k].Set(r.hash);
            const u32 base = static_cast<u32>(k * 4);
            bound = st.Bind(base + 1, keys[k].View());
            if (bound.Ok()) {
                bound = st.Bind(base + 2, ValueView::Integer(next_seq_ + static_cast<i64>(k)));
            }
            if (bound.Ok()) {
                bound = st.Bind(base + 3, hashes[k].View());
            }
            if (bound.Ok()) {
                bound = st.Bind(base + 4, ValueView::Blob(ByteSpan(r.blob.Data(), r.blob.Size())));
            }
        }
        database::Status stepped = bound;
        bool unused = false;
        if (stepped.Ok()) {
            stepped = st.Step(&unused);
        }
        static_cast<void>(st.Reset());
        SQL_TRY(stepped);
        next_seq_ += static_cast<i64>(take);
        for (usize k = 0; k < take; ++k) {
            ENGINE_TRY(WriteColumns(records[i + k]));
        }
        i += take;
    }
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
    ENGINE_TRY(RemoveColumns(key)); // before the entity row, whether or not the database enforces the foreign key
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

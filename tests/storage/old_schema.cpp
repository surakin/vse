#include "old_schema.h"

namespace engine_placeholder::storage::test
{

namespace
{
void BigEndian(u64 v, byte* out)
{
    for (usize i = 0; i < 8; ++i)
    {
        out[i] = static_cast<byte>((v >> (56 - 8 * i)) & 0xFF);
    }
}
}  // namespace

database::Status BuildSchemaOne(database::Connection& c, const Vector<EntityRecord>& records)
{
    database::Transaction tx(c);
    if (tx.BeginStatus().Failed())
    {
        return tx.BeginStatus();
    }
    database::Status s = c.Execute("CREATE TABLE meta (key TEXT NOT NULL PRIMARY KEY, value BLOB NOT NULL)");
    if (s.Failed())
    {
        return s;
    }
    s = c.Execute("CREATE TABLE entity (key BLOB NOT NULL PRIMARY KEY, hash BLOB NOT NULL, data BLOB NOT NULL)");
    if (s.Failed())
    {
        return s;
    }
    UniquePtr<database::Statement> put;
    s = c.Prepare("INSERT INTO entity (key, hash, data) VALUES (?, ?, ?)", &put);
    if (s.Failed())
    {
        return s;
    }
    for (usize i = 0; i < records.Size(); ++i)
    {
        byte key[16];
        byte hash[8];
        BigEndian(records[i].key.high, key);
        BigEndian(records[i].key.low, key + 8);
        BigEndian(records[i].hash, hash);
        s = put->Reset();
        if (s.Ok())
        {
            s = put->Bind(1, database::ValueView::Blob(ByteSpan(key, 16)));
        }
        if (s.Ok())
        {
            s = put->Bind(2, database::ValueView::Blob(ByteSpan(hash, 8)));
        }
        if (s.Ok())
        {
            s = put->Bind(3, database::ValueView::Blob(ByteSpan(records[i].blob.Data(), records[i].blob.Size())));
        }
        bool row = false;
        if (s.Ok())
        {
            s = put->Step(&row);
        }
        if (s.Failed())
        {
            return s;
        }
    }
    UniquePtr<database::Statement> meta;
    s = c.Prepare("INSERT INTO meta (key, value) VALUES (?, ?)", &meta);
    if (s.Failed())
    {
        return s;
    }
    const byte one = static_cast<byte>('1');
    s = meta->Bind(1, database::ValueView::Text("format_version"));
    if (s.Ok())
    {
        s = meta->Bind(2, database::ValueView::Blob(ByteSpan(&one, 1)));
    }
    bool row = false;
    if (s.Ok())
    {
        s = meta->Step(&row);
    }
    if (s.Failed())
    {
        return s;
    }
    s = c.WriteSchemaVersion(1);
    if (s.Failed())
    {
        return s;
    }
    return tx.Commit();
}

}  // namespace engine_placeholder::storage::test

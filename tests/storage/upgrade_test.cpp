// Versions, upgrade chains, unknown components, custom codecs, record framing and the storage limits (Phase 19 Day
// 257).

#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "storage/component_record.h"
#include "storage/component_registry.h"
#include "storage/storage_settings.h"

namespace engine_placeholder::storage
{
namespace
{

// Version 1: {x, y}. Version 2 adds z. Version 3 widens `flags` from a u8 to a u32 and adds `scale`.
struct Mover
{
    f32 x = 0;
    f32 y = 0;
    f32 z = 0;
    u32 flags = 0;
    f32 scale = 1.0f;
};

Status V1ToV2(bytes::ByteReader& in, bytes::ByteWriter& out, const CodecLimits&)
{
    f32 x = 0, y = 0;
    bytes::ByteError e = in.ReadF32(&x);
    if (e == bytes::ByteError::Ok)
    {
        e = in.ReadF32(&y);
    }
    if (e != bytes::ByteError::Ok)
    {
        return detail::FromByteError(e);
    }
    out.WriteF32(x);
    out.WriteF32(y);
    out.WriteF32(0.0f);  // z did not exist
    out.WriteU8(0);      // v2 has a u8 flags field
    return Status::Success();
}

Status V2ToV3(bytes::ByteReader& in, bytes::ByteWriter& out, const CodecLimits&)
{
    f32 x = 0, y = 0, z = 0;
    u8 flags = 0;
    bytes::ByteError e = in.ReadF32(&x);
    if (e == bytes::ByteError::Ok)
    {
        e = in.ReadF32(&y);
    }
    if (e == bytes::ByteError::Ok)
    {
        e = in.ReadF32(&z);
    }
    if (e == bytes::ByteError::Ok)
    {
        e = in.ReadU8(&flags);
    }
    if (e != bytes::ByteError::Ok)
    {
        return detail::FromByteError(e);
    }
    out.WriteF32(x);
    out.WriteF32(y);
    out.WriteF32(z);
    out.WriteU32(flags);
    out.WriteF32(1.0f);  // scale did not exist
    return Status::Success();
}

Status RegisterMover(ComponentRegistry* r, u32 id = 3000)
{
    return r->Register<Mover>({.id = id,
                               .name = "mover",
                               .version = 3,
                               .fields = {Field<&Mover::x>("x"), Field<&Mover::y>("y"), Field<&Mover::z>("z"),
                                          Field<&Mover::flags>("flags"), Field<&Mover::scale>("scale")},
                               .upgrades = {{1, &V1ToV2}, {2, &V2ToV3}}});
}

Vector<byte> V1Golden()
{
    // x = 1.0f, y = 2.0f (little-endian bit patterns).
    const u8 raw[] = {0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40};
    Vector<byte> v;
    for (const u8 b : raw)
    {
        v.Push(static_cast<byte>(b));
    }
    return v;
}

TEST_CASE("Upgrade: a version 1 record loads through a three-step chain and equals the current value",
          "[storage][upgrade]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterMover(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(3000);
    CHECK(c->Version() == 3);
    CHECK(c->OldestVersion() == 1);
    const Vector<byte> v1 = V1Golden();
    Mover loaded;
    REQUIRE(c->DecodeRecord(1, ByteSpan(v1.Data(), v1.Size()), &loaded, CodecLimits()).Ok());
    CHECK(loaded.x == 1.0f);
    CHECK(loaded.y == 2.0f);
    CHECK(loaded.z == 0.0f);
    CHECK(loaded.flags == 0);
    CHECK(loaded.scale == 1.0f);

    // The same value written by the current version encodes to the bytes the chain produced.
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&loaded, w, CodecLimits()).Ok());
    Mover again;
    REQUIRE(c->DecodeRecord(3, w.Bytes(), &again, CodecLimits()).Ok());
    CHECK(again.x == 1.0f);
    CHECK(again.scale == 1.0f);

    // A version 2 record starts the chain at its own step.
    bytes::ByteWriter v2;
    v2.WriteF32(5.0f);
    v2.WriteF32(6.0f);
    v2.WriteF32(7.0f);
    v2.WriteU8(9);
    Mover from2;
    REQUIRE(c->DecodeRecord(2, v2.Bytes(), &from2, CodecLimits()).Ok());
    CHECK(from2.z == 7.0f);
    CHECK(from2.flags == 9);
    CHECK(from2.scale == 1.0f);
}

TEST_CASE("Upgrade: versions outside the chain are refused", "[storage][upgrade]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterMover(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(3000);
    const Vector<byte> v1 = V1Golden();
    Mover m;
    CHECK(c->DecodeRecord(4, ByteSpan(v1.Data(), v1.Size()), &m, CodecLimits()).Is(ErrorCode::TooNew));
    CHECK(c->DecodeRecord(0, ByteSpan(v1.Data(), v1.Size()), &m, CodecLimits()).Is(ErrorCode::TooOld));

    ComponentRegistry narrow;
    REQUIRE(narrow
                .Register<Mover>({.id = 3001,
                                  .name = "mover2",
                                  .version = 3,
                                  .oldest_version = 2,
                                  .fields = {Field<&Mover::x>("x"), Field<&Mover::y>("y"), Field<&Mover::z>("z"),
                                             Field<&Mover::flags>("flags"), Field<&Mover::scale>("scale")},
                                  .upgrades = {{2, &V2ToV3}}})
                .Ok());
    CHECK(narrow.FindById(3001)
              ->DecodeRecord(1, ByteSpan(v1.Data(), v1.Size()), &m, CodecLimits())
              .Is(ErrorCode::TooOld));
}

TEST_CASE("Upgrade: a chain with a gap, a duplicate or a bad step fails at registration", "[storage][upgrade]")
{
    auto fields =
        [](ComponentRegistry* r, u32 id, const char* name, u32 version, u32 oldest, InitializerList<UpgradeStep> steps)
    {
        return r->Register<Mover>({.id = id,
                                   .name = name,
                                   .version = version,
                                   .oldest_version = oldest,
                                   .fields = {Field<&Mover::x>("x")},
                                   .upgrades = steps});
    };
    ComponentRegistry r;
    CHECK(fields(&r, 1, "gap", 3, 1, {{1, &V1ToV2}}).Is(ErrorCode::Invalid));  // step 2 missing
    CHECK(fields(&r, 2, "dup", 3, 1, {{1, &V1ToV2}, {1, &V1ToV2}})
              .Is(ErrorCode::Invalid));                                          // step 1 twice, step 2 missing
    CHECK(fields(&r, 3, "range", 2, 1, {{2, &V1ToV2}}).Is(ErrorCode::Invalid));  // from_version outside the chain
    CHECK(fields(&r, 4, "null", 2, 1, {{1, nullptr}}).Is(ErrorCode::Invalid));
    CHECK(fields(&r, 5, "none", 2, 1, {}).Is(ErrorCode::Invalid));    // version 2 with no step
    CHECK(fields(&r, 6, "oldest", 2, 3, {}).Is(ErrorCode::Invalid));  // oldest above the version
    CHECK(fields(&r, 7, "zero", 2, 0, {{1, &V1ToV2}}).Is(ErrorCode::Invalid));
    CHECK(fields(&r, 8, "fine", 2, 1, {{1, &V1ToV2}}).Ok());
    CHECK(r.Count() == 1);
}

TEST_CASE("Upgrade: bad input to a step, trailing bytes and oversize records are errors", "[storage][upgrade]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterMover(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(3000);
    Mover m;
    const Vector<byte> v1 = V1Golden();
    for (usize cut = 0; cut < v1.Size(); ++cut)
    {
        CHECK(c->DecodeRecord(1, ByteSpan(v1.Data(), cut), &m, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    Vector<byte> longer = V1Golden();
    longer.Push(byte{0});
    CHECK(c->DecodeRecord(1, ByteSpan(longer.Data(), longer.Size()), &m, CodecLimits())
              .Is(ErrorCode::Corrupt));  // the first step leaves a byte unread
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&m, w, CodecLimits()).Ok());
    w.WriteU8(1);
    CHECK(c->DecodeRecord(3, w.Bytes(), &m, CodecLimits()).Is(ErrorCode::Corrupt));  // bytes left after the last field
    CodecLimits tight;
    tight.max_record_bytes = 4;
    CHECK(c->DecodeRecord(1, ByteSpan(v1.Data(), v1.Size()), &m, tight).Is(ErrorCode::LimitExceeded));
    tight.max_record_bytes = 8;  // the record fits, the upgraded form (17 bytes) does not
    CHECK(c->DecodeRecord(1, ByteSpan(v1.Data(), v1.Size()), &m, tight).Is(ErrorCode::LimitExceeded));
}

TEST_CASE("Upgrade: mutated records through the chain are a Status, never a crash", "[storage][upgrade][fuzz]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterMover(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(3000);
    u64 state = 0x1234567ull;
    auto next = [&state]()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545f4914f6cdd1dull;
    };
    u32 ok = 0;
    u32 bad = 0;
    for (u32 n = 0; n < 20000; ++n)
    {
        Vector<byte> bytes = V1Golden();
        const u32 version = 1 + static_cast<u32>(next() % 3);
        if (version == 2)
        {  // a plausible version 2 record: three floats and a flag byte
            bytes.Resize(13);
        }
        else if (version == 3)
        {
            bytes.Resize(20);
        }
        for (usize i = 8; i < bytes.Size(); ++i)
        {
            bytes[i] = static_cast<byte>(next());
        }
        switch (next() % 3)
        {
        case 0: bytes[static_cast<usize>(next() % bytes.Size())] = static_cast<byte>(next()); break;
        case 1: bytes.Resize(static_cast<usize>(next() % (bytes.Size() + 1))); break;
        default: bytes.Push(static_cast<byte>(next())); break;
        }
        Mover m;
        const Status s = c->DecodeRecord(version, ByteSpan(bytes.Data(), bytes.Size()), &m, CodecLimits());
        if (s.Ok())
        {
            ++ok;
        }
        else
        {
            ++bad;
            CHECK(s.Is(ErrorCode::Corrupt));
        }
    }
    CHECK(ok > 0);
    CHECK(bad > 0);
}

TEST_CASE("Unknown components follow the policy and unknown bytes survive a load and save", "[storage][upgrade]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterMover(&registry).Ok());
    REQUIRE(registry.Register<Mover>({.id = 3002, .name = "dup_type"})
                .Failed());  // a type registers once; the id stays free
    struct Derived
    {
        u32 v = 0;
    };
    REQUIRE(registry.Register<Derived>({.id = 3003, .name = "derived", .flags = kComponentDerived}).Ok());

    const RegisteredComponent* comp = nullptr;
    RecordAction action = RecordAction::Skip;
    CHECK(registry.Resolve(3000, UnknownPolicy::Fail, &comp, &action).Ok());
    CHECK(action == RecordAction::Decode);
    CHECK(comp == registry.FindById(3000));
    CHECK(registry.Resolve(3003, UnknownPolicy::Fail, &comp, &action).Ok());  // derived: dropped, never an error
    CHECK(action == RecordAction::Skip);
    CHECK(comp == nullptr);
    CHECK(registry.Resolve(9999, UnknownPolicy::Fail, &comp, &action).Is(ErrorCode::NotFound));
    CHECK(registry.Resolve(9999, UnknownPolicy::SkipAndReport, &comp, &action).Ok());
    CHECK(action == RecordAction::Skip);
    CHECK(registry.Resolve(9999, UnknownPolicy::KeepOpaque, &comp, &action).Ok());
    CHECK(action == RecordAction::KeepOpaque);

    // A stand-in loader and saver: a stored entity with one known and one unknown component.
    Mover original;
    original.x = 4;
    original.flags = 7;
    bytes::ByteWriter known;
    REQUIRE(registry.FindById(3000)->Encode(&original, known, CodecLimits()).Ok());
    bytes::ByteWriter blob;
    ComponentRecord a;
    a.id = 3000;
    a.version = 3;
    a.bytes = known.Bytes();
    const u8 unknown_raw[] = {1, 2, 3, 0xff, 0x00, 0x7f};
    Vector<byte> unknown;
    for (const u8 b : unknown_raw)
    {
        unknown.Push(static_cast<byte>(b));
    }
    ComponentRecord b;
    b.id = 7777;
    b.version = 5;
    b.bytes = ByteSpan(unknown.Data(), unknown.Size());
    REQUIRE(WriteComponentRecord(a, CodecLimits(), blob).Ok());
    REQUIRE(WriteComponentRecord(b, CodecLimits(), blob).Ok());

    Vector<OpaqueComponent> kept;
    Mover loaded;
    bytes::ByteReader reader(blob.Bytes());
    while (!reader.AtEnd())
    {
        ComponentRecord rec;
        REQUIRE(ReadComponentRecord(reader, CodecLimits(), &rec).Ok());
        REQUIRE(registry.Resolve(rec.id, UnknownPolicy::KeepOpaque, &comp, &action).Ok());
        if (action == RecordAction::Decode)
        {
            REQUIRE(comp->DecodeRecord(rec.version, rec.bytes, &loaded, CodecLimits()).Ok());
        }
        else if (action == RecordAction::KeepOpaque)
        {
            kept.Push(OpaqueComponent::From(rec));
        }
    }
    CHECK(loaded.x == 4.0f);
    CHECK(loaded.flags == 7);
    REQUIRE(kept.Size() == 1);

    bytes::ByteWriter saved;
    bytes::ByteWriter fresh;
    REQUIRE(registry.FindById(3000)->Encode(&loaded, fresh, CodecLimits()).Ok());
    ComponentRecord a2;
    a2.id = 3000;
    a2.version = 3;
    a2.bytes = fresh.Bytes();
    REQUIRE(WriteComponentRecord(a2, CodecLimits(), saved).Ok());
    REQUIRE(WriteComponentRecord(kept[0].View(), CodecLimits(), saved).Ok());
    REQUIRE(saved.Size() == blob.Size());
    CHECK(MemCompare(saved.Bytes().Data(), blob.Bytes().Data(), blob.Size()) == 0);
}

TEST_CASE("Record framing: golden bytes, and every cut or bad header is an error", "[storage][upgrade]")
{
    const u8 payload[] = {0xaa, 0xbb};
    ComponentRecord r;
    r.id = 0x01020304;
    r.version = 2;
    r.bytes = ByteSpan(reinterpret_cast<const byte*>(payload), 2);
    bytes::ByteWriter w;
    REQUIRE(WriteComponentRecord(r, CodecLimits(), w).Ok());
    const u8 golden[] = {0x04, 0x03, 0x02, 0x01, 0x02, 0x00, 0x00, 0x00, 0x02, 0xaa, 0xbb};
    REQUIRE(w.Size() == sizeof(golden));
    for (usize i = 0; i < sizeof(golden); ++i)
    {
        CHECK(static_cast<u8>(w.Bytes()[i]) == golden[i]);
    }
    for (usize cut = 0; cut < w.Size(); ++cut)
    {
        bytes::ByteReader in(ByteSpan(w.Bytes().Data(), cut));
        ComponentRecord out;
        CHECK(ReadComponentRecord(in, CodecLimits(), &out).Is(ErrorCode::Corrupt));
        CHECK(in.Position() == 0);
    }
    CodecLimits tight;
    tight.max_record_bytes = 1;
    bytes::ByteReader in(w.Bytes());
    ComponentRecord out;
    CHECK(ReadComponentRecord(in, tight, &out).Is(ErrorCode::LimitExceeded));
    CHECK(WriteComponentRecord(r, tight, w).Is(ErrorCode::LimitExceeded));
    ComponentRecord zero;
    CHECK(WriteComponentRecord(zero, CodecLimits(), w).Is(ErrorCode::Invalid));
    const u8 id0[] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    bytes::ByteReader z(ByteSpan(reinterpret_cast<const byte*>(id0), sizeof(id0)));
    CHECK(ReadComponentRecord(z, CodecLimits(), &out).Is(ErrorCode::Corrupt));
}

struct Handle
{
    u32 index = 0;
    u32 generation = 0;
};
Status SaveHandle(const void* component, bytes::ByteWriter& out, const CodecLimits&)
{
    const Handle* h = static_cast<const Handle*>(component);
    out.WriteVarint((static_cast<u64>(h->generation) << 32) | h->index);
    return Status::Success();
}
Status LoadHandle(bytes::ByteReader& in, void* component, const CodecLimits&)
{
    u64 packed = 0;
    const bytes::ByteError e = in.ReadVarint(&packed);
    if (e != bytes::ByteError::Ok)
    {
        return detail::FromByteError(e);
    }
    Handle* h = static_cast<Handle*>(component);
    h->index = static_cast<u32>(packed & 0xffffffffu);
    h->generation = static_cast<u32>(packed >> 32);
    return Status::Success();
}

TEST_CASE("Custom codec: a hand-written pair is used, with the same bounds", "[storage][upgrade]")
{
    ComponentRegistry registry;
    REQUIRE(registry.Register<Handle>({.id = 3100, .name = "handle", .custom = {&SaveHandle, &LoadHandle}}).Ok());
    const RegisteredComponent* c = registry.FindById(3100);
    Handle in;
    in.index = 123456;
    in.generation = 99;
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&in, w, CodecLimits()).Ok());
    Handle out;
    REQUIRE(c->DecodeRecord(1, w.Bytes(), &out, CodecLimits()).Ok());
    CHECK(out.index == 123456);
    CHECK(out.generation == 99);
    for (usize cut = 0; cut < w.Size(); ++cut)
    {
        Handle h;
        CHECK(c->DecodeRecord(1, ByteSpan(w.Bytes().Data(), cut), &h, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    // Registration rules for a custom codec.
    ComponentRegistry r;
    CHECK(r.Register<Handle>({.id = 1, .name = "half", .custom = {&SaveHandle, nullptr}}).Is(ErrorCode::Invalid));
    CHECK(r.Register<Handle>(
               {.id = 2, .name = "both", .fields = {Field<&Handle::index>("i")}, .custom = {&SaveHandle, &LoadHandle}})
              .Is(ErrorCode::Invalid));
    CHECK(
        r.Register<Handle>({.id = 3, .name = "skipped", .flags = kComponentSkip, .custom = {&SaveHandle, &LoadHandle}})
            .Is(ErrorCode::Invalid));
    CHECK(r.Count() == 0);
}

TEST_CASE("Limits: the four storage limits are settings and reach the codec", "[storage][upgrade][settings]")
{
    settings::Registry registry;
    RegisterStorageSettings(&registry);
    RegisterStorageSettings(&registry);
    CodecLimits defaults = LimitsFromSettings(registry);
    CHECK(defaults.max_record_bytes == CodecLimits().max_record_bytes);
    CHECK(defaults.max_string_bytes == CodecLimits().max_string_bytes);
    CHECK(defaults.max_array_elements == CodecLimits().max_array_elements);
    CHECK(defaults.max_components_per_entity == CodecLimits().max_components_per_entity);
    registry.Publish(registry.Find(StringView("storage.max_record_bytes")), settings::Value::FromInt(100));
    registry.Publish(registry.Find(StringView("storage.max_string_bytes")), settings::Value::FromInt(10));
    registry.Publish(registry.Find(StringView("storage.max_array_elements")), settings::Value::FromInt(3));
    registry.Publish(registry.Find(StringView("storage.max_components_per_entity")), settings::Value::FromInt(5));
    const CodecLimits l = LimitsFromSettings(registry);
    CHECK(l.max_record_bytes == 100);
    CHECK(l.max_string_bytes == 10);
    CHECK(l.max_array_elements == 3);
    CHECK(l.max_components_per_entity == 5);
}

}  // namespace
}  // namespace engine_placeholder::storage

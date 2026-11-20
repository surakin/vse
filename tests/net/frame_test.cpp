// Length-prefixed framing (Phase 20 Day 271): every split of the byte stream, hostile lengths, and the memory a lying
// header can cost.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "net/frame.h"

namespace vse::net
{
namespace
{

u64 FrameTestNext(u64* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

// A frame's payload is a function of its index, so the receiver can check it without keeping a copy.
void FrameTestPayload(usize index, usize size, Vector<byte>* out)
{
    out->Clear();
    u64 state = 0x9e3779b97f4a7c15ull ^ (index * 0x100000001b3ull);
    for (usize i = 0; i < size; ++i)
    {
        out->Push(static_cast<byte>(FrameTestNext(&state) & 0xff));
    }
}

usize FrameTestSize(usize index)
{
    u64 state = 0xdeadbeefull + index;
    return 1 + FrameTestNext(&state) % 200;
}

// Feeds `stream` to `reader` in pieces whose sizes come from `piece`, checking each frame; returns the frames seen.
template <class PieceSize>
usize FrameTestFeed(FrameReader& reader, const Vector<byte>& stream, usize frames, PieceSize piece, bool* ok)
{
    Vector<byte> expected;
    usize seen = 0;
    usize at = 0;
    while (at < stream.Size())
    {
        usize want = piece(at);
        want = want < stream.Size() - at ? want : stream.Size() - at;
        usize offset = 0;
        while (offset < want)
        {
            usize used = 0;
            if (reader.Consume(ByteSpan(stream.Data() + at + offset, want - offset), &used).Failed())
            {
                *ok = false;
                return seen;
            }
            offset += used;
            if (reader.HasFrame())
            {
                FrameTestPayload(seen, FrameTestSize(seen), &expected);
                const ByteSpan frame = reader.Frame();
                if (frame.Size() != expected.Size() || MemCompare(frame.Data(), expected.Data(), frame.Size()) != 0)
                {
                    *ok = false;
                    return seen;
                }
                reader.Pop();
                ++seen;
            }
        }
        at += want;
    }
    *ok = seen == frames && !reader.InFrame();
    return seen;
}

void FrameTestBuildStream(usize frames, const FrameWriter& writer, Vector<byte>* stream)
{
    Vector<byte> payload;
    for (usize i = 0; i < frames; ++i)
    {
        FrameTestPayload(i, FrameTestSize(i), &payload);
        REQUIRE(writer.Encode(ByteSpan(payload.Data(), payload.Size()), stream).Ok());
    }
}

}  // namespace

TEST_CASE("A frame is a big-endian length and the payload", "[net][frame]")
{
    FrameWriter writer(1000);
    Vector<byte> out;
    const byte payload[3] = {byte{1}, byte{2}, byte{3}};
    REQUIRE(writer.Encode(ByteSpan(payload, 3), &out).Ok());
    REQUIRE(out.Size() == 7);
    CHECK(out[0] == byte{0});
    CHECK(out[1] == byte{0});
    CHECK(out[2] == byte{0});
    CHECK(out[3] == byte{3});
    CHECK(out[4] == byte{1});
    CHECK(out[6] == byte{3});
}

TEST_CASE("The writer refuses an empty or oversized payload and leaves the output alone", "[net][frame]")
{
    FrameWriter writer(8);
    Vector<byte> out;
    out.Push(byte{9});
    byte big[9] = {};
    CHECK(writer.Encode(ByteSpan(big, 9), &out).Is(ErrorCode::LimitExceeded));
    CHECK(writer.Encode(ByteSpan(), &out).Is(ErrorCode::Invalid));
    CHECK(out.Size() == 1);
    CHECK(writer.Encode(ByteSpan(big, 8), &out).Ok());
    CHECK(out.Size() == 1 + 4 + 8);
}

TEST_CASE("Ten thousand frames survive every way of cutting the stream", "[net][frame]")
{
    constexpr usize kFrames = 10000;
    FrameWriter writer(1000);
    Vector<byte> stream;
    FrameTestBuildStream(kFrames, writer, &stream);

    SECTION("one byte at a time")
    {
        FrameReader reader(1000);
        bool ok = true;
        CHECK(FrameTestFeed(reader, stream, kFrames, [](usize) { return usize{1}; }, &ok) == kFrames);
        CHECK(ok);
    }
    SECTION("pieces of random size")
    {
        for (u64 seed = 1; seed <= 5; ++seed)
        {
            FrameReader reader(1000);
            u64 state = seed * 7919;
            bool ok = true;
            CHECK(FrameTestFeed(
                      reader, stream, kFrames,
                      [&state](usize) { return static_cast<usize>(1 + FrameTestNext(&state) % 700); }, &ok) == kFrames);
            CHECK(ok);
        }
    }
    SECTION("the whole stream at once")
    {
        FrameReader reader(1000);
        bool ok = true;
        CHECK(FrameTestFeed(reader, stream, kFrames, [&stream](usize) { return stream.Size(); }, &ok) == kFrames);
        CHECK(ok);
    }
}

TEST_CASE("The stream cut at every possible single point decodes the same", "[net][frame]")
{
    constexpr usize kFrames = 6;
    FrameWriter writer(1000);
    Vector<byte> stream;
    FrameTestBuildStream(kFrames, writer, &stream);
    for (usize cut = 0; cut <= stream.Size(); ++cut)
    {
        FrameReader reader(1000);
        bool ok = true;
        // Two pieces: the first `cut` bytes, then the rest.
        const usize seen = FrameTestFeed(
            reader, stream, kFrames, [cut](usize at) { return at == 0 && cut > 0 ? cut : ~usize{0}; }, &ok);
        CHECK(seen == kFrames);
        CHECK(ok);
    }
}

TEST_CASE("A length above the limit fails before any payload memory is used", "[net][frame]")
{
    FrameReader reader(1000);
    const byte header[4] = {byte{0}, byte{0}, byte{0x03}, byte{0xe9}};  // 1001
    usize used = 0;
    CHECK(reader.Consume(ByteSpan(header, 4), &used).Is(ErrorCode::LimitExceeded));
    CHECK(used == 4);
    CHECK(reader.BufferedBytes() <= 4);
    // The failure is sticky.
    const byte more[2] = {byte{1}, byte{2}};
    CHECK(reader.Consume(ByteSpan(more, 2), &used).Is(ErrorCode::LimitExceeded));
    CHECK(used == 0);
    reader.Reset();
    const byte fine[5] = {byte{0}, byte{0}, byte{0}, byte{1}, byte{7}};
    CHECK(reader.Consume(ByteSpan(fine, 5), &used).Ok());
    CHECK(reader.HasFrame());
}

TEST_CASE("A zero length and a huge length are rejected", "[net][frame]")
{
    {
        FrameReader reader(1000);
        const byte zero[4] = {};
        usize used = 0;
        CHECK(reader.Consume(ByteSpan(zero, 4), &used).Is(ErrorCode::Invalid));
    }
    {
        FrameReader reader(1000);
        const byte huge[4] = {byte{0xff}, byte{0xff}, byte{0xff}, byte{0xff}};
        usize used = 0;
        CHECK(reader.Consume(ByteSpan(huge, 4), &used).Is(ErrorCode::LimitExceeded));
    }
}

TEST_CASE("A header that promises the limit but delivers little holds little", "[net][frame]")
{
    FrameReader reader(1u << 20);
    const byte header[4] = {byte{0}, byte{0x10}, byte{0}, byte{0}};  // 1 MiB
    usize used = 0;
    REQUIRE(reader.Consume(ByteSpan(header, 4), &used).Ok());
    const byte bits[10] = {};
    REQUIRE(reader.Consume(ByteSpan(bits, 10), &used).Ok());
    CHECK(used == 10);
    CHECK(reader.InFrame());
    CHECK_FALSE(reader.HasFrame());
    CHECK(reader.BufferedBytes() == 14);  // the 4 header bytes and the 10 payload bytes that arrived, not 1 MiB
}

TEST_CASE("The reader stops after a frame and leaves the following bytes", "[net][frame]")
{
    FrameWriter writer(100);
    Vector<byte> stream;
    const byte a[2] = {byte{1}, byte{2}};
    const byte b[1] = {byte{3}};
    REQUIRE(writer.Encode(ByteSpan(a, 2), &stream).Ok());
    REQUIRE(writer.Encode(ByteSpan(b, 1), &stream).Ok());
    FrameReader reader(100);
    usize used = 0;
    REQUIRE(reader.Consume(ByteSpan(stream.Data(), stream.Size()), &used).Ok());
    CHECK(used == 6);
    REQUIRE(reader.HasFrame());
    CHECK(reader.Frame().Size() == 2);
    // While a frame waits, nothing more is taken.
    usize none = 99;
    CHECK(reader.Consume(ByteSpan(stream.Data() + used, stream.Size() - used), &none).Ok());
    CHECK(none == 0);
    reader.Pop();
    REQUIRE(reader.Consume(ByteSpan(stream.Data() + used, stream.Size() - used), &none).Ok());
    CHECK(none == 5);
    REQUIRE(reader.HasFrame());
    CHECK(reader.Frame().Size() == 1);
}

}  // namespace vse::net

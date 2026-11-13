// The skeleton format, its source form and the minter (Phase 22 Day 318).

#include <catch2/catch_test_macros.hpp>

#include "asset/skeleton.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "skeleton_source.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr const char* kSkelIdLine = "id 00000000000000aa\n";

Vector<char> SkelText(const char* text)
{
    Vector<char> out;
    for (; *text != '\0'; ++text)
    {
        out.Push(*text);
    }
    return out;
}

void SkelAppend(Vector<char>* out, const char* text)
{
    for (; *text != '\0'; ++text)
    {
        out->Push(*text);
    }
}

StringView SkelView(const Vector<char>& v)
{
    return StringView(v.Data(), v.Size());
}

// A chain of `n` joints: j0 root, jK child of j(K-1), each moved along y and turned a little about z.
Vector<char> SkelChain(u32 n)
{
    Vector<char> text = SkelText(kSkelIdLine);
    for (u32 i = 0; i < n; ++i)
    {
        char line[200];
        const f64 half = 0.1 * (i + 1);
        char parent[8];
        FormatTo(parent, sizeof(parent), i == 0 ? "-" : "j%u", i - 1);
        FormatTo(line, sizeof(line), "joint j%u %s 0 %f 0.%u  0 0 %f %f  1 1 1\n", i, parent, 1.0 + 0.1 * i, i,
                 Sin(half), Cos(half));
        SkelAppend(&text, line);
    }
    return text;
}

Vector<byte> SkelMint(const TypeRegistry& types, const Vector<char>& text)
{
    Vector<byte> blob;
    SkeletonSourceError error;
    const Status s = MintSkeleton(SkelView(text), types, &blob, &error);
    INFO(error.message.Data());
    REQUIRE(s.Ok());
    return blob;
}

Vector<byte> SkelCopyOf(const Vector<byte>& bytes)
{
    Vector<byte> out;
    out.Resize(bytes.Size());
    MemCopy(out.Data(), bytes.Data(), bytes.Size());
    return out;
}

// Rebuilds `blob` with section `id` replaced by `replacement` (or removed when `remove`).
Vector<byte> SkelWithSection(const TypeRegistry& types, const Vector<byte>& blob, u32 id,
                             const Vector<byte>& replacement, bool remove = false)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    BlobWriter writer(types, types::kSkeleton, reader.Id(), 0);
    for (u32 section = kFirstUserSection; section < kFirstUserSection + 8; ++section)
    {
        ByteSpan data;
        if (section == id)
        {
            if (!remove)
            {
                REQUIRE(writer.AddSection(section, ByteSpan(replacement.Data(), replacement.Size())).Ok());
            }
        }
        else if (reader.FindSection(section, &data))
        {
            REQUIRE(writer.AddSection(section, data).Ok());
        }
    }
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

Vector<byte> SkelSectionCopy(const TypeRegistry& types, const Vector<byte>& blob, u32 id)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan data;
    REQUIRE(reader.FindSection(id, &data));
    Vector<byte> out;
    out.Resize(data.Size());
    MemCopy(out.Data(), data.Data(), data.Size());
    return out;
}

Status SkelOpenView(const TypeRegistry& types, const Vector<byte>& blob)
{
    SkeletonView view;
    return view.Open(ByteSpan(blob.Data(), blob.Size()), types);
}

struct SkelBadSource
{
    const char* body;
    u32 line;
    ErrorCode code;
};

}  // namespace

TEST_CASE("skeleton: the type is registered", "[asset][skeleton]")
{
    TypeRegistry types;
    TypeInfo info;
    REQUIRE(types.Find(types::kSkeleton, &info));
    CHECK(info.current_version == 1);
}

TEST_CASE("skeleton source: each error names its line", "[asset][skeleton]")
{
    TypeRegistry types;
    const SkelBadSource cases[] = {
        {"joint a - 0 0 0 0 0 0 1 1 1 1\n", 1, ErrorCode::Invalid},                         // joint before id
        {"id 00000000000000aa\nid 00000000000000aa\n", 2, ErrorCode::Invalid},              // id twice
        {"id nothex\n", 1, ErrorCode::Invalid},                                             // bad id
        {"id 00000000000000aa\n\n# c\nthing a\n", 4, ErrorCode::Invalid},                   // unknown record
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 1 1 1\n", 2, ErrorCode::Invalid},      // too few fields
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 1 1 1 x\n", 2, ErrorCode::Invalid},    // not a number
        {"id 00000000000000aa\njoint a - 0 0 nan 0 0 0 1 1 1 1\n", 2, ErrorCode::Invalid},  // NaN
        {"id 00000000000000aa\njoint a b 0 0 0 0 0 0 1 1 1 1\n", 2, ErrorCode::Invalid},    // unknown parent
        {"id 00000000000000aa\njoint a a 0 0 0 0 0 0 1 1 1 1\n", 2, ErrorCode::Invalid},    // its own parent (a cycle)
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 1 1 1 1\njoint a - 0 0 0 0 0 0 1 1 1 1\n", 3,
         ErrorCode::Invalid},                                                             // duplicate name
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 2 1 1 1\n", 2, ErrorCode::Invalid},  // not unit
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 0 1 1 1\n", 2, ErrorCode::Invalid},  // zero quaternion
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 1 1 0 1\n", 2, ErrorCode::Invalid},  // zero scale
        {"id 00000000000000aa\nibm a 1 0 0 0 0 1 0 0 0 0 1 0\n", 2, ErrorCode::Invalid},  // unknown joint
        {"id 00000000000000aa\njoint a - 0 0 0 0 0 0 1 1 1 1\nibm a 1 0 0 0 0 1 0 0 0 0 1\n", 3,
         ErrorCode::Invalid},                              // too few numbers
        {"id 00000000000000aa\n", 0, ErrorCode::Invalid},  // no joints
        {"joint", 1, ErrorCode::Invalid},                  // no id, no newline
    };
    for (const SkelBadSource& c : cases)
    {
        INFO(c.body);
        Vector<byte> blob;
        SkeletonSourceError error;
        const Status s = MintSkeleton(StringView(c.body, CStrLength(c.body)), types, &blob, &error);
        CHECK(s.Code() == c.code);
        CHECK(error.line == c.line);
        CHECK(CStrLength(error.message.Data()) > 0);
    }
    // A joint line with no parent-before-child order is the same "unknown parent" error.
    {
        Vector<byte> blob;
        SkeletonSourceError error;
        const char* later = "id 00000000000000aa\njoint a b 0 0 0 0 0 0 1 1 1 1\njoint b - 0 0 0 0 0 0 1 1 1 1\n";
        CHECK(MintSkeleton(StringView(later, CStrLength(later)), types, &blob, &error).Failed());
        CHECK(error.line == 2);
    }
}

TEST_CASE("skeleton source: a long name and the 257th joint are refused", "[asset][skeleton]")
{
    TypeRegistry types;
    {
        Vector<char> text = SkelText(kSkelIdLine);
        SkelAppend(&text, "joint ");
        for (u32 i = 0; i < 65; ++i)
        {
            SkelAppend(&text, "n");
        }
        SkelAppend(&text, " - 0 0 0 0 0 0 1 1 1 1\n");
        Vector<byte> blob;
        SkeletonSourceError error;
        CHECK(MintSkeleton(SkelView(text), types, &blob, &error).Code() == ErrorCode::Invalid);
        CHECK(error.line == 2);
    }
    {
        Vector<byte> blob;
        SkeletonSourceError error;
        CHECK(MintSkeleton(SkelView(SkelChain(256)), types, &blob, &error).Ok());
        const Status s = MintSkeleton(SkelView(SkelChain(257)), types, &blob, &error);
        CHECK(s.Code() == ErrorCode::LimitExceeded);
        CHECK(error.line == 258);
    }
}

TEST_CASE("skeleton: a chain mints and reloads to the same joint tables", "[asset][skeleton]")
{
    TypeRegistry types;
    const Vector<char> text = SkelChain(5);
    SkeletonSource source;
    SkeletonSourceError error;
    REQUIRE(ParseSkeletonSource(SkelView(text), &source, &error).Ok());
    const Vector<byte> blob = SkelMint(types, text);

    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    REQUIRE(view.JointCount() == 5);
    CHECK(view.HasNames());
    for (u32 i = 0; i < 5; ++i)
    {
        CHECK(view.Parent(i) == source.joints[i].parent);
        for (u32 k = 0; k < 3; ++k)
        {
            CHECK(view.Translation(i)[k] == source.joints[i].translation[k]);
            CHECK(view.Scale(i)[k] == source.joints[i].scale[k]);
        }
        for (u32 k = 0; k < 4; ++k)
        {
            CHECK(view.Rotation(i)[k] == source.joints[i].rotation[k]);
        }
        char name[8];
        FormatTo(name, sizeof(name), "j%u", i);
        CHECK(view.Name(i) == StringView(name));
        u32 found = 99;
        REQUIRE(view.FindJoint(StringView(name), &found));
        CHECK(found == i);
    }
    CHECK(view.Parent(0) == kNoParent);
    u32 none = 0;
    CHECK_FALSE(view.FindJoint(StringView("missing"), &none));
    CHECK(view.BoundRadius() > 0.0f);
}

TEST_CASE("skeleton: a 20-joint humanoid shape reloads and inverse bind times model is the identity",
          "[asset][skeleton]")
{
    TypeRegistry types;
    // A spine of 6 joints with two 4-joint arms and two 3-joint legs off it, with scales and turns.
    Vector<char> text = SkelText(kSkelIdLine);
    SkelAppend(&text, "joint root - 0 1 0  0 0 0 1  1 1 1\n");
    SkelAppend(&text, "joint spine1 root 0 0.2 0  0.1305262 0 0 0.9914449  1 1 1\n");
    SkelAppend(&text, "joint spine2 spine1 0 0.2 0  0 0.1305262 0 0.9914449  1 1 1\n");
    SkelAppend(&text, "joint chest spine2 0 0.2 0  0 0 0.1305262 0.9914449  1.1 1 1\n");
    SkelAppend(&text, "joint neck chest 0 0.3 0  0 0 0 1  1 1 1\n");
    SkelAppend(&text, "joint head neck 0 0.15 0  0.2588190 0 0 0.9659258  1 1.2 1\n");
    const char* sides[2] = {"l", "r"};
    for (u32 s = 0; s < 2; ++s)
    {
        const f64 sign = s == 0 ? 1.0 : -1.0;
        char line[256];
        const char* sd = sides[s];
        FormatTo(line, sizeof(line), "joint shoulder_%s chest %f 0.25 0  0 0 %f 0.7071068  1 1 1\n", sd, 0.2 * sign,
                 0.7071068 * sign);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint elbow_%s shoulder_%s %f 0 0  0 0.3826834 0 0.9238795  1 1 1\n", sd, sd,
                 0.3 * sign);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint wrist_%s elbow_%s %f 0 0  0 0 0 1  1 1 1\n", sd, sd, 0.25 * sign);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint hand_%s wrist_%s %f 0 0  0 0 0 1  1 1 1\n", sd, sd, 0.1 * sign);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint hip_%s root %f -0.05 0  0 0 0 1  1 1 1\n", sd, 0.1 * sign);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint knee_%s hip_%s 0 -0.45 0  0.1305262 0 0 0.9914449  1 1 1\n", sd, sd);
        SkelAppend(&text, line);
        FormatTo(line, sizeof(line), "joint foot_%s knee_%s 0 -0.45 0  0 0 0 1  1 1 1\n", sd, sd);
        SkelAppend(&text, line);
    }
    // 6 + 2 * 7 = 20 joints.
    const Vector<byte> blob = SkelMint(types, text);
    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    REQUIRE(view.JointCount() == 20);

    // Model matrices from the loaded bind pose (double precision) times the stored inverse bind.
    f64 model[20][12];
    for (u32 i = 0; i < 20; ++i)
    {
        const Array<f32, 4> q = view.Rotation(i);
        const Array<f32, 3> t = view.Translation(i);
        const Array<f32, 3> sc = view.Scale(i);
        const f64 x = q[0], y = q[1], z = q[2], w = q[3];
        const f64 r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                          2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                          2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
        f64 local[12];
        for (u32 row = 0; row < 3; ++row)
        {
            for (u32 col = 0; col < 3; ++col)
            {
                local[row * 4 + col] = r[row * 3 + col] * sc[col];
            }
            local[row * 4 + 3] = t[row];
        }
        if (view.Parent(i) == kNoParent)
        {
            MemCopy(model[i], local, sizeof(local));
        }
        else
        {
            const f64* p = model[view.Parent(i)];
            for (u32 row = 0; row < 3; ++row)
            {
                for (u32 col = 0; col < 4; ++col)
                {
                    f64 v = col == 3 ? p[row * 4 + 3] : 0.0;
                    for (u32 k = 0; k < 3; ++k)
                    {
                        v += p[row * 4 + k] * local[k * 4 + col];
                    }
                    model[i][row * 4 + col] = v;
                }
            }
        }
        const Array<f32, 12> ibm = view.InverseBind(i);
        for (u32 row = 0; row < 3; ++row)
        {
            for (u32 col = 0; col < 4; ++col)
            {
                f64 v = col == 3 ? ibm[row * 4 + 3] : 0.0;
                for (u32 k = 0; k < 3; ++k)
                {
                    v += static_cast<f64>(ibm[row * 4 + k]) * model[i][k * 4 + col];
                }
                const f64 expected = (row == col) ? 1.0 : 0.0;
                if (col < 3)
                {
                    CHECK(Abs(v - expected) < 1e-5);
                }
                else
                {
                    CHECK(Abs(v) < 1e-5);
                }
            }
        }
    }
}

TEST_CASE("skeleton: an ibm line overrides the computed matrix", "[asset][skeleton]")
{
    TypeRegistry types;
    Vector<char> text = SkelChain(2);
    SkelAppend(&text, "ibm j1 1 0 0 5  0 1 0 6  0 0 1 7\n");
    const Vector<byte> blob = SkelMint(types, text);
    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    const Array<f32, 12> m = view.InverseBind(1);
    CHECK(m[3] == 5.0f);
    CHECK(m[7] == 6.0f);
    CHECK(m[11] == 7.0f);
    CHECK(view.InverseBind(0)[3] != 5.0f);
}

TEST_CASE("skeleton: a quaternion in the lower hemisphere is stored with w >= 0", "[asset][skeleton]")
{
    TypeRegistry types;
    Vector<char> text = SkelText(kSkelIdLine);
    SkelAppend(&text, "joint a - 0 0 0  0 0 -0.6 -0.8  1 1 1\n");
    const Vector<byte> blob = SkelMint(types, text);
    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.Rotation(0)[3] > 0.0f);
    CHECK(Abs(view.Rotation(0)[2] - 0.6f) < 1e-6f);
}

TEST_CASE("skeleton: minting twice is byte-identical, and the hash follows the hierarchy and the names",
          "[asset][skeleton]")
{
    TypeRegistry types;
    const Vector<byte> a = SkelMint(types, SkelChain(7));
    const Vector<byte> b = SkelMint(types, SkelChain(7));
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);

    SkeletonView va, vc;
    REQUIRE(va.Open(ByteSpan(a.Data(), a.Size()), types).Ok());
    const Vector<byte> c = SkelMint(types, SkelChain(6));
    REQUIRE(vc.Open(ByteSpan(c.Data(), c.Size()), types).Ok());
    CHECK(va.Hash() != vc.Hash());

    // The same hierarchy and names with a different bind pose has the same skeleton hash.
    Vector<char> moved = SkelChain(7);
    SkelAppend(&moved, "");
    Vector<char> other = SkelText(kSkelIdLine);
    for (u32 i = 0; i < 7; ++i)
    {
        char line[160];
        char parent[8];
        FormatTo(parent, sizeof(parent), i == 0 ? "-" : "j%u", i - 1);
        FormatTo(line, sizeof(line), "joint j%u %s 9 9 9  0 0 0 1  2 2 2\n", i, parent);
        SkelAppend(&other, line);
    }
    const Vector<byte> d = SkelMint(types, other);
    SkeletonView vd;
    REQUIRE(vd.Open(ByteSpan(d.Data(), d.Size()), types).Ok());
    CHECK(va.Hash() == vd.Hash());
}

TEST_CASE("skeleton: the inverse bind section is 64-byte aligned with a 48-byte stride", "[asset][skeleton]")
{
    TypeRegistry types;
    const Vector<byte> blob = SkelMint(types, SkelChain(9));
    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    const ByteSpan ibm = view.InverseBindBytes();
    CHECK(ibm.Size() == 9 * kInverseBindBytes);
    CHECK(kInverseBindBytes == 48);
    CHECK(static_cast<usize>(ibm.Data() - blob.Data()) % kSectionAlignment == 0);
    // The bytes are exactly the accessor's values: no conversion between the file and the upload.
    const Array<f32, 12> m = view.InverseBind(3);
    CHECK(MemCompare(m.Data(), ibm.Data() + 3 * kInverseBindBytes, kInverseBindBytes) == 0);
}

TEST_CASE("skeleton: hostile blobs are refused", "[asset][skeleton]")
{
    TypeRegistry types;
    const Vector<byte> good = SkelMint(types, SkelChain(5));
    REQUIRE(SkelOpenView(types, good).Ok());

    SECTION("a parent after its child")
    {
        Vector<byte> parents = SkelSectionCopy(types, good, kSkeletonSectionParents);
        parents[2] = 3;  // joint 1's parent is joint 3
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionParents, parents)).Code() ==
              ErrorCode::Corrupt);
    }
    SECTION("a parent equal to the joint itself")
    {
        Vector<byte> parents = SkelSectionCopy(types, good, kSkeletonSectionParents);
        parents[4] = 2;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionParents, parents)).Failed());
    }
    SECTION("a parent index out of range")
    {
        Vector<byte> parents = SkelSectionCopy(types, good, kSkeletonSectionParents);
        parents[2] = 0x80;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionParents, parents)).Failed());
    }
    SECTION("a joint count that does not match the sections")
    {
        Vector<byte> header = SkelSectionCopy(types, good, kSkeletonSectionHeader);
        header[0] = 6;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionHeader, header)).Failed());
        header[0] = 0;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionHeader, header)).Failed());
        header[0] = 0xff;
        header[1] = 0xff;
        header[2] = 0xff;
        header[3] = 0xff;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionHeader, header)).Failed());
    }
    SECTION("a header flag this version does not define")
    {
        Vector<byte> header = SkelSectionCopy(types, good, kSkeletonSectionHeader);
        header[5] = 1;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionHeader, header)).Failed());
    }
    SECTION("a non-unit bind quaternion")
    {
        Vector<byte> rotations = SkelSectionCopy(types, good, kSkeletonSectionRotations);
        const f32 big = 2.0f;
        MemCopy(rotations.Data() + 16, &big, 4);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionRotations, rotations)).Code() ==
              ErrorCode::Corrupt);
    }
    SECTION("a quaternion with negative w")
    {
        Vector<byte> rotations = SkelSectionCopy(types, good, kSkeletonSectionRotations);
        f32 q[4];
        MemCopy(q, rotations.Data(), 16);
        for (f32& c : q)
        {
            c = -c;
        }
        MemCopy(rotations.Data(), q, 16);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionRotations, rotations)).Failed());
    }
    SECTION("a NaN or zero scale")
    {
        Vector<byte> scales = SkelSectionCopy(types, good, kSkeletonSectionScales);
        const f32 zero = 0.0f;
        MemCopy(scales.Data() + 12, &zero, 4);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionScales, scales)).Failed());
        const f32 nan = Limits<f32>::QuietNaN();
        MemCopy(scales.Data() + 12, &nan, 4);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionScales, scales)).Failed());
    }
    SECTION("a non-finite inverse bind value")
    {
        Vector<byte> ibm = SkelSectionCopy(types, good, kSkeletonSectionInverseBind);
        const f32 inf = Limits<f32>::Infinity();
        MemCopy(ibm.Data() + 5 * 4, &inf, 4);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionInverseBind, ibm)).Failed());
    }
    SECTION("a section of the wrong size, and a missing section")
    {
        Vector<byte> ibm = SkelSectionCopy(types, good, kSkeletonSectionInverseBind);
        ibm.Resize(ibm.Size() - 4);
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionInverseBind, ibm)).Failed());
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionTranslations, Vector<byte>(), true))
                  .Failed());
    }
    SECTION("name offsets past the end, not increasing, or not starting at zero")
    {
        Vector<byte> names = SkelSectionCopy(types, good, kSkeletonSectionNames);
        Vector<byte> bad = SkelCopyOf(names);
        bad[4] = 0xff;  // the end of name 0 is far past the characters
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionNames, bad)).Failed());
        bad = SkelCopyOf(names);
        bad[0] = 1;  // the first offset must be zero
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionNames, bad)).Failed());
        bad = SkelCopyOf(names);
        bad[8] = bad[4];  // an empty name (equal offsets)
        bad[9] = 0;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionNames, bad)).Failed());
        bad = SkelCopyOf(names);
        bad.Resize(bad.Size() - 1);  // the last name is cut
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionNames, bad)).Failed());
    }
    SECTION("a name hash that does not match its name")
    {
        Vector<byte> hashes = SkelSectionCopy(types, good, kSkeletonSectionNameHashes);
        hashes[0] ^= 1;
        CHECK(SkelOpenView(types, SkelWithSection(types, good, kSkeletonSectionNameHashes, hashes)).Failed());
    }
    SECTION("a blob of another type, truncation and a non-current version")
    {
        BlobWriter other(types, types::kNames, ResourceId::FromValue(5), 0);
        Vector<byte> not_a_skeleton;
        REQUIRE(other.Finish(&not_a_skeleton).Ok());
        CHECK(SkelOpenView(types, not_a_skeleton).Failed());

        for (usize cut = 0; cut < good.Size(); cut += 7)
        {
            SkeletonView view;
            CHECK(view.Open(ByteSpan(good.Data(), cut), types).Failed());
        }

        Vector<byte> v0 = SkelCopyOf(good);
        v0[12] = 0;  // type version 0
        const Status s = SkelOpenView(types, v0);
        CHECK(s.Code() == ErrorCode::UnsupportedVersion);
        CHECK(StringView(s.Message()).Contains(StringView("asset_mint remint")));
    }
}

TEST_CASE("skeleton: BuildSkeletonBlob refuses what the minter would never produce", "[asset][skeleton]")
{
    TypeRegistry types;
    SkeletonSource source;
    SkeletonSourceError error;
    REQUIRE(ParseSkeletonSource(SkelView(SkelChain(3)), &source, &error).Ok());
    ComputeInverseBindMatrices(Span<SkeletonJoint>(source.joints.Data(), source.joints.Size()));
    Vector<byte> out;
    const ResourceId id = source.id;
    auto build = [&]()
    {
        return BuildSkeletonBlob(types, id, Span<const SkeletonJoint>(source.joints.Data(), source.joints.Size()), true,
                                 &out);
    };
    CHECK(build().Ok());
    source.joints[2].parent = 2;
    CHECK(build().Failed());
    source.joints[2].parent = 1;
    source.joints[1].rotation[0] = 1.0f;
    CHECK(build().Failed());
    source.joints[1].rotation[0] = 0.0f;
    source.joints[0].translation[1] = Limits<f32>::QuietNaN();
    CHECK(build().Failed());
    CHECK(BuildSkeletonBlob(types, id, Span<const SkeletonJoint>(), true, &out).Failed());
    // A name that fills the whole array with no terminator is refused, not read past the end.
    source.joints[0].translation[1] = 0.0f;
    for (usize i = 0; i < source.joints[0].name.Size(); ++i)
    {
        source.joints[0].name[i] = 'x';
    }
    CHECK(build().Code() == ErrorCode::Invalid);
}

TEST_CASE("skeleton: a skeleton without names loads and finds nothing by name", "[asset][skeleton]")
{
    TypeRegistry types;
    SkeletonSource source;
    SkeletonSourceError error;
    REQUIRE(ParseSkeletonSource(SkelView(SkelChain(3)), &source, &error).Ok());
    ComputeInverseBindMatrices(Span<SkeletonJoint>(source.joints.Data(), source.joints.Size()));
    Vector<byte> blob;
    REQUIRE(BuildSkeletonBlob(types, source.id, Span<const SkeletonJoint>(source.joints.Data(), source.joints.Size()),
                              false, &blob)
                .Ok());
    SkeletonView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK_FALSE(view.HasNames());
    u32 index = 0;
    CHECK_FALSE(view.FindJoint(StringView("j0"), &index));
    CHECK(view.Name(0).Empty());
    // The hash is the same with or without names stored.
    const Vector<byte> with = SkelMint(types, SkelChain(3));
    SkeletonView v2;
    REQUIRE(v2.Open(ByteSpan(with.Data(), with.Size()), types).Ok());
    CHECK(v2.Hash() == view.Hash());
}

}  // namespace engine_placeholder::asset

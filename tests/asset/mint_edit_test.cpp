// Phase 22 Day 334: the reference-safe edits of asset_mint, the target transforms, the upgrade chains and the version
// checks (project.h, target_transform.h, upgrade.h).

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/pak.h"
#include "asset/texture.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "io/file.h"
#include "io/file_system.h"
#include "material_source.h"
#include "project.h"
#include "support.h"
#include "texture_source.h"

namespace vse::asset
{
namespace
{

void EditPut(const io::Path& root, const char* rel, const char* text)
{
    const io::Path path = root / StringView(rel);
    REQUIRE(io::CreateDirectories(path.Parent()).Ok());
    REQUIRE(io::WriteFile(path, text, CStrLength(text)).Ok());
}

void Sample(const io::Path& root)
{
    EditPut(root, "assets.project", "source_root src\noutput_root out\npak sample.pak\n");
    EditPut(root, "src/skeletons/hero.skel.src",
            "id 0000000000000a01\njoint root - 0 0 0  0 0 0 1  1 1 1\njoint arm root 0 1 0  0 0 0 1  1 1 1\n");
    EditPut(
        root, "src/meshes/hero.mesh.src",
        "id 0000000000000b01\nskeleton 0000000000000a01\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\n"
        "v 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\nskin 0 0 1\nskin 1 0 1\nskin 2 1 1\n");
    EditPut(
        root, "src/meshes/rock.mesh.src",
        "id 0000000000000b02\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n");
    EditPut(
        root, "src/clips/idle.anim.src",
        "id 0000000000000c01\nskeleton 0000000000000a01\nduration 1\nrate 30\nflags loop\ntrack arm\nt 0 0 1 0\nt 1 0 2 0\n");
    EditPut(root, "src/textures/brick.tex.src",
            "id 0000000000000d01\nsource brick.raw\nformat rgba8\nsrgb true\nmips full\n");
    Vector<byte> pixels;
    for (u32 i = 0; i < 4 * 4 * 4; ++i)
    {
        pixels.Push(static_cast<byte>(i * 3 + 1));
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(4, 4, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Ok());
    REQUIRE(io::WriteFile(root / StringView("src/textures/brick.raw"), raw.Data(), raw.Size()).Ok());
    EditPut(root, "src/materials/brick.mat.src",
            "id 0000000000000e01\ntemplate gbuffer\nparam roughness float 0.5\nparam base_color color 1 0.5 0.25 1\n"
            "texture albedo_tex /textures/brick\nsampler linear_sampler filter nearest address clamp anisotropy 4\n"
            "state cull none\nstate alpha_blend on\nstate depth_compare lessequal\n");
    EditPut(
        root, "src/levels/main.level.src",
        "id 0000000000000f01\nentity rock\n  component local_transform\n    position = 1 0 2\n  component mesh_reference\n"
        "    mesh = /meshes/rock\n    material = /materials/brick\n");
}

Vector<u8> EditRead(const io::Path& path)
{
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    return bytes;
}

bool EditSame(const Vector<u8>& a, const Vector<u8>& b)
{
    return a.Size() == b.Size() && MemCompare(a.Data(), b.Data(), a.Size()) == 0;
}

Status MintAll(const io::Path& root)
{
    Project project;
    MintDiagnostics diagnostics;
    ENGINE_TRY(project.Load(root / StringView("assets.project"), &diagnostics));
    MintStats stats;
    return project.Mint(MintOptions(), &diagnostics, &stats);
}

}  // namespace

TEST_CASE("asset_mint: rename and move rewrite the references and leave every dependent's blob unchanged",
          "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    REQUIRE(MintAll(dir.Path()).Ok());
    const Vector<u8> material = EditRead(dir.Path() / StringView("out/materials/brick.mat.blob"));
    const Vector<u8> level = EditRead(dir.Path() / StringView("out/levels/main.level.blob"));
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Rename(StringView("/textures/brick"), StringView("stone"), &diagnostics).Ok());
    }
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Move(StringView("/meshes/rock"), StringView("props/deep"), &diagnostics).Ok());
    }
    CHECK(io::Exists(dir.Path() / StringView("src/textures/stone.tex.src")));
    CHECK(io::Exists(dir.Path() / StringView("src/props/deep/rock.mesh.src")));
    CHECK_FALSE(io::Exists(dir.Path() / StringView("src/textures/brick.tex.src")));
    // The sources that named them now name the new paths.
    const Vector<u8> mat_source = EditRead(dir.Path() / StringView("src/materials/brick.mat.src"));
    CHECK(StringView(reinterpret_cast<const char*>(mat_source.Data()), mat_source.Size())
              .Contains(StringView("/textures/stone")));
    REQUIRE(MintAll(dir.Path()).Ok());
    CHECK(EditSame(material, EditRead(dir.Path() / StringView("out/materials/brick.mat.blob"))));
    CHECK(EditSame(level, EditRead(dir.Path() / StringView("out/levels/main.level.blob"))));
    CHECK_FALSE(io::Exists(dir.Path() / StringView("out/textures/brick.tex.blob")));  // the old blob is gone
    // A name that is taken is refused and nothing changes.
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    MintDiagnostics taken;
    CHECK(project.Rename(StringView("/props/deep/rock"), StringView("rock"), &taken).Failed());
    CHECK(io::Exists(dir.Path() / StringView("src/props/deep/rock.mesh.src")));
}

TEST_CASE("asset_mint: a referenced asset cannot be deleted, and nothing changes", "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    MintDiagnostics refused;
    CHECK(project.Delete(StringView("/textures/brick"), false, &refused).Failed());
    CHECK(refused.Mentions(StringView("materials/brick.mat.src")));
    CHECK(io::Exists(dir.Path() / StringView("src/textures/brick.tex.src")));
    MintDiagnostics listed;
    CHECK(project.Delete(StringView("/textures/brick"), true, &listed).Failed());
    CHECK(listed.Mentions(StringView("levels/main.level.src")));  // what would have to go first, through the material
    CHECK(io::Exists(dir.Path() / StringView("src/textures/brick.tex.src")));
    // Nothing references the level: it goes.
    MintDiagnostics fine;
    CHECK(project.Delete(StringView("/levels/main"), false, &fine).Ok());
    CHECK_FALSE(io::Exists(dir.Path() / StringView("src/levels/main.level.src")));
}

TEST_CASE("asset_mint: reid fixes two sources that share an id, and refuses to orphan a reference",
          "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    EditPut(
        dir.Path(), "src/meshes/rock_copy.mesh.src",
        "id 0000000000000b02\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n");
    {
        Project project;
        MintDiagnostics diagnostics;
        CHECK(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Failed());  // the shared id
        CHECK(project.Reid(StringView("meshes/rock_copy.mesh.src"), false, &diagnostics).Ok());
    }
    REQUIRE(MintAll(dir.Path()).Ok());
    // The skeleton is referenced by id (by the mesh and the clip): reid refuses, and rewrites them on request.
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    MintDiagnostics refused;
    CHECK(project.Reid(StringView("/skeletons/hero"), false, &refused).Failed());
    CHECK(refused.Mentions(StringView("meshes/hero.mesh.src")));
    CHECK(refused.Mentions(StringView("clips/idle.anim.src")));
    const Vector<u8> unchanged = EditRead(dir.Path() / StringView("src/skeletons/hero.skel.src"));
    CHECK(EditSame(unchanged, EditRead(dir.Path() / StringView("src/skeletons/hero.skel.src"))));
    MintDiagnostics updated;
    REQUIRE(project.Reid(StringView("/skeletons/hero"), true, &updated).Ok());
    REQUIRE(MintAll(dir.Path()).Ok());
}

TEST_CASE("asset_mint: a moved material and a moved texture keep what they name relative to their folder",
          "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    // A material that names its texture relative to its own folder, and the texture's image in that folder.
    EditPut(dir.Path(), "src/materials/tex/x.tex.src",
            "id 0000000000000d09\nsource x.raw\nformat rgba8\nsrgb true\nmips 1\n");
    Vector<byte> pixels;
    for (u32 i = 0; i < 4 * 4 * 4; ++i)
    {
        pixels.Push(static_cast<byte>(i));
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(4, 4, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Ok());
    REQUIRE(io::WriteFile(dir.Path() / StringView("src/materials/tex/x.raw"), raw.Data(), raw.Size()).Ok());
    EditPut(dir.Path(), "src/materials/local.mat.src",
            "id 0000000000000e09\ntemplate gbuffer\ntexture albedo_tex tex/x\n");
    REQUIRE(MintAll(dir.Path()).Ok());
    const Vector<u8> before = EditRead(dir.Path() / StringView("out/materials/local.mat.blob"));
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Move(StringView("/materials/local"), StringView("elsewhere"), &diagnostics).Ok());
    }
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Move(StringView("/materials/tex/x"), StringView("moved"), &diagnostics).Ok());
    }
    CHECK(io::Exists(dir.Path() / StringView("src/moved/x.raw")));  // the image came along
    CHECK_FALSE(io::Exists(dir.Path() / StringView("src/materials/tex/x.raw")));
    REQUIRE(MintAll(dir.Path()).Ok());
    CHECK(EditSame(before, EditRead(dir.Path() / StringView("out/elsewhere/local.mat.blob"))));
}

TEST_CASE("asset_mint: copy refuses a source with no id", "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    EditPut(dir.Path(), "src/meshes/bare.mesh.src", "v 0 0 0  0 0 1  1 0 0 1  0 0\n");
    Project project;
    MintDiagnostics diagnostics;
    CHECK(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Failed());  // the missing id
    MintDiagnostics refused;
    CHECK(project.Copy(StringView("meshes/bare.mesh.src"), StringView("/props/bare2"), &refused).Failed());
    CHECK(refused.Mentions(StringView("asset_mint adopt")));
    CHECK_FALSE(io::Exists(dir.Path() / StringView("src/props/bare2.mesh.src")));
}

TEST_CASE("asset_mint: copy makes a source with a fresh id that mints", "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Copy(StringView("/meshes/rock"), StringView("/props/rock2"), &diagnostics).Ok());
        CHECK(project.Copy(StringView("/meshes/rock"), StringView("/props/rock2"), &diagnostics).Failed());  // taken
    }
    REQUIRE(MintAll(dir.Path()).Ok());
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    CHECK(project.SourceCount() == 8);
    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(project.Refs(StringView("/props/rock2"), &diagnostics, &text).Ok());
    CHECK(text.View().Contains(StringView("props/rock2.mesh.src")));
}

TEST_CASE("asset_mint: unmint writes a material and a level back as text that mints the same blob",
          "[asset][mint][edit]")
{
    test::TempDir dir;
    Sample(dir.Path());
    REQUIRE(MintAll(dir.Path()).Ok());
    const Vector<u8> material = EditRead(dir.Path() / StringView("out/materials/brick.mat.blob"));
    const Vector<u8> level = EditRead(dir.Path() / StringView("out/levels/main.level.blob"));
    String<HeapAllocator> material_text(HeapAllocator::Default());
    String<HeapAllocator> level_text(HeapAllocator::Default());
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Unmint(StringView("/materials/brick"), &diagnostics, &material_text).Ok());
        REQUIRE(project.Unmint(StringView("/levels/main"), &diagnostics, &level_text).Ok());
        CHECK(project.Unmint(StringView("/meshes/rock"), &diagnostics, &level_text).Failed());  // not text-able
    }
    // The text replaces the sources; minting them again gives the very same blobs.
    EditPut(dir.Path(), "src/materials/brick.mat.src", material_text.CStr());
    EditPut(dir.Path(), "src/levels/main.level.src", level_text.CStr());
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    MintStats stats;
    REQUIRE(project.Mint(MintOptions(), &diagnostics, &stats).Ok());
    CHECK(stats.rewritten == 0);  // the bytes are what was on disk
    CHECK(EditSame(material, EditRead(dir.Path() / StringView("out/materials/brick.mat.blob"))));
    CHECK(EditSame(level, EditRead(dir.Path() / StringView("out/levels/main.level.blob"))));
}

TEST_CASE("asset_mint: packing for another target relays textures out, retags the rest and is deterministic",
          "[asset][mint][target]")
{
    test::TempDir dir;
    Sample(dir.Path());
    REQUIRE(MintAll(dir.Path()).Ok());
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    PackOptions target1;
    target1.has_target = true;
    target1.target = 1;
    REQUIRE(project.Pack(&diagnostics, target1).Ok());
    const Vector<u8> first = EditRead(dir.Path() / StringView("out/sample.t1.pak"));
    REQUIRE(project.Pack(&diagnostics, target1).Ok());
    CHECK(EditSame(first, EditRead(dir.Path() / StringView("out/sample.t1.pak"))));  // two packs, the same bytes
    PackOptions own;
    REQUIRE(project.Pack(&diagnostics, own).Ok());  // the project's own target: nothing is transformed
    VerifyOptions verify1;
    verify1.has_target = true;
    verify1.target = 1;
    CHECK(project.Verify(&diagnostics, verify1).Ok());
    VerifyOptions verify0;
    CHECK(project.Verify(&diagnostics, verify0).Ok());
    // A target whose pak was never made is an error, not a success.
    VerifyOptions missing_pak;
    missing_pak.has_target = true;
    missing_pak.target = 2;
    MintDiagnostics no_pak;
    CHECK(project.Verify(&no_pak, missing_pak).Failed());
    CHECK(no_pak.Mentions(StringView("asset_mint pack --target 2")));
    // A pak for one target is refused by the runtime for another.
    TypeRegistry types;
    io::FilePtr file;
    REQUIRE(
        io::File::Open(dir.Path() / StringView("out/sample.t1.pak"), io::OpenMode::Read, io::kOpenNone, &file).Ok());
    PakReader reader;
    CHECK(PakReader::Open(static_cast<io::FilePtr&&>(file), types, 0, &reader).Is(ErrorCode::WrongTarget));
    // The pak holds every resource, under the paths the sources have.
    io::FilePtr again;
    REQUIRE(
        io::File::Open(dir.Path() / StringView("out/sample.t1.pak"), io::OpenMode::Read, io::kOpenNone, &again).Ok());
    PakReader inside;
    REQUIRE(PakReader::Open(static_cast<io::FilePtr&&>(again), types, 1, &inside).Ok());
    CHECK(inside.Count() == 7);
    // A target no layout is known for fails, and names the texture.
    PackOptions unknown;
    unknown.has_target = true;
    unknown.target = 9;
    MintDiagnostics refused;
    CHECK(project.Pack(&refused, unknown).Failed());
    CHECK(refused.Mentions(StringView("textures/brick")));
    CHECK_FALSE(io::Exists(dir.Path() / StringView("out/sample.t9.pak")));
}

TEST_CASE("target transforms: the texture relayout, the retag, and the rules a transform must keep",
          "[asset][mint][target]")
{
    TypeRegistry types;
    Vector<byte> pixels;
    for (u32 i = 0; i < 5 * 3 * 4; ++i)
    {
        pixels.Push(static_cast<byte>(i));
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(5, 3, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Ok());
    const char* source = "id 0000000000000d02\nsource x.raw\nformat rgba8\nmips 1\n";
    Span<const ByteSpan> images;
    const ByteSpan image(raw.Data(), raw.Size());
    images = Span<const ByteSpan>(&image, 1);
    Vector<byte> blob;
    TextureSourceError error;
    REQUIRE(MintTexture(StringView(source), images, types, &blob, &error, 0).Ok());

    const TargetTransforms transforms = TargetTransforms::Builtin();
    TransformContext context;
    context.types = &types;
    context.target = 1;
    Vector<byte> out;
    REQUIRE(TransformBlob(transforms, context, ByteSpan(blob.Data(), blob.Size()), &out).Ok());
    TextureView view;
    REQUIRE(view.Open(ByteSpan(out.Data(), out.Size()), types).Ok());
    CHECK(view.Level(0, 0).row_pitch % 256 == 0);  // the row alignment of target 1
    Vector<byte> again;
    REQUIRE(TransformBlob(transforms, context, ByteSpan(blob.Data(), blob.Size()), &again).Ok());
    CHECK(out.Size() == again.Size());
    CHECK(MemCompare(out.Data(), again.Data(), out.Size()) == 0);  // deterministic
    // A blob already laid out for the target is copied.
    context.target = 0;
    Vector<byte> same;
    REQUIRE(TransformBlob(transforms, context, ByteSpan(blob.Data(), blob.Size()), &same).Ok());
    CHECK(same.Size() == blob.Size());
    CHECK(MemCompare(same.Data(), blob.Data(), blob.Size()) == 0);

    // A type with no transform is retagged: only the target tag and the hash differ.
    const TypeRegistry registry = test::MakeBackendRegistry();
    const Vector<byte> plain = test::MakeTestBlob(registry, test::Rid(0x77), 100, 5);
    context.types = &registry;
    context.target = 2;
    Vector<byte> tagged;
    REQUIRE(TransformBlob(transforms, context, ByteSpan(plain.Data(), plain.Size()), &tagged).Ok());
    BlobHeaderInfo info;
    REQUIRE(PeekBlobHeader(ByteSpan(tagged.Data(), tagged.Size()), &info).Ok());
    CHECK(info.target == 2);
    CHECK(tagged.Size() == plain.Size());
    CHECK(MemCompare(tagged.Data() + 64, plain.Data() + 64, plain.Size() - 64) == 0);

    // A transform that adds a reference is refused; so is one that leaves the target tag alone.
    auto adds_reference = [](const TransformContext& c, ByteSpan input, Vector<byte>* result) -> Status
    {
        BlobReader reader;
        ENGINE_TRY(reader.Open(input, *c.types));
        BlobWriter writer(*c.types, reader.Type(), reader.Id(), c.target);
        writer.AddDependency(Dependency{test::Rid(0x99), reader.Type(), 1, 0});
        return writer.Finish(result);
    };
    auto keeps_target = [](const TransformContext&, ByteSpan input, Vector<byte>* result) -> Status
    {
        result->Resize(input.Size());
        MemCopy(result->Data(), input.Data(), input.Size());
        return Status::Success();
    };
    // A transform that gives different bytes on two runs is refused.
    static u32 calls = 0;
    auto changes_each_time = [](const TransformContext& c, ByteSpan input, Vector<byte>* result) -> Status
    {
        Vector<byte> tagged;
        ENGINE_TRY(RetagBlob(input, c.target, &tagged));
        BlobReader reader;
        ENGINE_TRY(reader.Open(ByteSpan(tagged.Data(), tagged.Size()), *c.types));
        ByteSpan payload;
        if (!reader.FindSection(16, &payload))
        {
            return Status::Make(ErrorCode::Corrupt, "no payload");
        }
        BlobWriter writer(*c.types, reader.Type(), reader.Id(), c.target);
        Vector<byte> changed;
        for (usize i = 0; i < payload.Size(); ++i)
        {
            changed.Push(payload[i]);
        }
        changed[0] = static_cast<byte>(++calls);
        ENGINE_TRY(writer.AddSection(16, ByteSpan(changed.Data(), changed.Size())));
        return writer.Finish(result);
    };
    TargetTransforms random;
    random.Register(test::kBackendTestType, +changes_each_time);
    Vector<byte> unstable;
    const Status flaky = TransformBlob(random, context, ByteSpan(plain.Data(), plain.Size()), &unstable);
    CHECK(flaky.Failed());
    TargetTransforms bad;
    bad.Register(test::kBackendTestType, +adds_reference);
    Vector<byte> refused;
    const Status added = TransformBlob(bad, context, ByteSpan(plain.Data(), plain.Size()), &refused);
    CHECK(added.Failed());
    bad.Register(test::kBackendTestType, +keeps_target);
    CHECK(TransformBlob(bad, context, ByteSpan(plain.Data(), plain.Size()), &refused).Failed());
}

namespace
{

// The old and the new version of one type: the registry of the tool knows version 2, the old blobs are version 1.
constexpr ResourceType kUpgradeType = ResourceType::FromChars('U', 'P', 'G', 'R');

TypeRegistry RegistryAt(u32 version)
{
    TypeRegistry registry;
    REQUIRE(registry.Register(kUpgradeType, version, "upgrade test").Ok());
    return registry;
}

Vector<byte> BlobAt(u32 version, ResourceId id, u32 seed)
{
    const TypeRegistry registry = RegistryAt(version);
    Vector<byte> payload;
    for (u32 i = 0; i < 64; ++i)
    {
        payload.Push(static_cast<byte>(seed + i));
    }
    BlobWriter writer(registry, kUpgradeType, id);
    REQUIRE(writer.AddSection(16, payload).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

// Version 2 is version 1 with the payload kept: the step reads the old blob with the old registry.
Status UpgradeOneToTwo(const TypeRegistry& types, ByteSpan old_blob, Vector<byte>* out)
{
    const TypeRegistry old = RegistryAt(1);
    BlobReader reader;
    ENGINE_TRY(reader.Open(old_blob, old));
    ByteSpan payload;
    if (!reader.FindSection(16, &payload))
    {
        return Status::Make(ErrorCode::Corrupt, "no payload");
    }
    BlobWriter writer(types, kUpgradeType, reader.Id(), reader.Target());
    ENGINE_TRY(writer.AddSection(16, payload));
    return writer.Finish(out);
}

}  // namespace

TEST_CASE("asset_mint_old_versions_fixture: verify names old blobs, remint upgrades them, verify passes",
          "[asset][mint][version]")
{
    test::TempDir dir;
    const Vector<byte> a = BlobAt(1, test::Rid(0x501), 1);
    const Vector<byte> b = BlobAt(1, test::Rid(0x502), 2);
    REQUIRE(io::CreateDirectories(dir.Path() / StringView("sub")).Ok());
    REQUIRE(io::WriteFile(dir.Path() / StringView("a.blob"), a.Data(), a.Size()).Ok());
    REQUIRE(io::WriteFile(dir.Path() / StringView("sub/b.blob"), b.Data(), b.Size()).Ok());
    const TypeRegistry current = RegistryAt(2);
    VerifyOptions require;
    require.require_current = true;
    MintDiagnostics failing;
    CHECK(VerifyDirectory(dir.Path(), io::Path(), 0, current, require, &failing).Failed());
    CHECK(failing.Mentions(StringView("a.blob")));
    CHECK(failing.Mentions(StringView("sub/b.blob")));
    CHECK(failing.Mentions(StringView("asset_mint remint")));
    // A blob newer than the tool is an error whatever the options: remint cannot make it older.
    {
        test::TempDir newer;
        const TypeRegistry future = RegistryAt(3);
        Vector<byte> payload;
        payload.Push(static_cast<byte>(1));
        BlobWriter writer(future, kUpgradeType, test::Rid(0x509));
        REQUIRE(writer.AddSection(16, payload).Ok());
        Vector<byte> blob;
        REQUIRE(writer.Finish(&blob).Ok());
        REQUIRE(io::WriteFile(newer.Path() / StringView("n.blob"), blob.Data(), blob.Size()).Ok());
        MintDiagnostics from_future;
        CHECK(VerifyDirectory(newer.Path(), io::Path(), 0, current, VerifyOptions(), &from_future).Failed());
        CHECK(from_future.Mentions(StringView("newer")));
    }
    // Without --require-current an old blob is only checked by its header and hash.
    MintDiagnostics tolerant;
    CHECK(VerifyDirectory(dir.Path(), io::Path(), 0, current, VerifyOptions(), &tolerant).Ok());
    // No step: remint fails and names them.
    UpgradeChains none;
    MintDiagnostics missing;
    RemintStats stats;
    CHECK(RemintDirectory(dir.Path(), current, none, &missing, &stats).Failed());
    CHECK(stats.failed == 2);
    CHECK(missing.Mentions(StringView("a.blob")));
    // With the step they are upgraded, in place, to what a fresh mint of the same content gives.
    UpgradeChains chains;
    chains.Register(kUpgradeType, 1, &UpgradeOneToTwo);
    MintDiagnostics done;
    REQUIRE(RemintDirectory(dir.Path(), current, chains, &done, &stats).Ok());
    CHECK(stats.upgraded == 2);
    MintDiagnostics after;
    CHECK(VerifyDirectory(dir.Path(), io::Path(), 0, current, require, &after).Ok());
    const Vector<byte> fresh = [&]
    {
        Vector<byte> payload;
        for (u32 i = 0; i < 64; ++i)
        {
            payload.Push(static_cast<byte>(1 + i));
        }
        BlobWriter writer(current, kUpgradeType, test::Rid(0x501));
        REQUIRE(writer.AddSection(16, payload).Ok());
        Vector<byte> blob;
        REQUIRE(writer.Finish(&blob).Ok());
        return blob;
    }();
    const Vector<u8> upgraded = EditRead(dir.Path() / StringView("a.blob"));
    CHECK(upgraded.Size() == fresh.Size());
    CHECK(MemCompare(upgraded.Data(), fresh.Data(), fresh.Size()) == 0);
    // A second run finds everything current.
    MintDiagnostics again;
    REQUIRE(RemintDirectory(dir.Path(), current, chains, &again, &stats).Ok());
    CHECK(stats.current == 2);
    CHECK(stats.upgraded == 0);
}

}  // namespace vse::asset

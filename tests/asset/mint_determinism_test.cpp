// Phase 22 Day 333: the project side of asset_mint (project.h): discovery, errors, incremental minting, parallel
// minting and determinism, pack and verify.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/pak.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "io/file.h"
#include "io/file_system.h"
#include "project.h"
#include "support.h"
#include "texture_source.h"

namespace vse::asset
{
namespace
{

void Put(const io::Path& root, const char* rel, const char* text)
{
    const io::Path path = root / StringView(rel);
    REQUIRE(io::CreateDirectories(path.Parent()).Ok());
    REQUIRE(io::WriteFile(path, text, CStrLength(text)).Ok());
}

struct SampleFile
{
    const char* rel;
    const char* id;    // the `id` line of the source
    const char* body;  // the rest
};

const SampleFile kSample[] = {
    {"src/skeletons/hero.skel.src", "0000000000000a01",
     "joint root - 0 0 0  0 0 0 1  1 1 1\njoint arm root 0 1 0  0 0 0 1  1 1 1\n"},
    {"src/meshes/hero.mesh.src", "0000000000000b01",
     "skeleton 0000000000000a01\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\n"
     "v 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\nskin 0 0 1\nskin 1 0 1\nskin 2 1 1\n"},
    {"src/meshes/rock.mesh.src", "0000000000000b02",
     "v 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n"},
    {"src/clips/idle.anim.src", "0000000000000c01",
     "skeleton 0000000000000a01\nduration 1\nrate 30\nflags loop\ntrack arm\nt 0 0 1 0\nt 1 0 2 0\n"},
    {"src/textures/brick.tex.src", "0000000000000d01", "source brick.raw\nformat rgba8\nsrgb true\nmips full\n"},
    {"src/materials/brick.mat.src", "0000000000000e01",
     "template gbuffer\nparam roughness float 0.5\ntexture albedo_tex /textures/brick\n"},
    {"src/levels/main.level.src", "0000000000000f01",
     "entity rock\n  component local_transform\n    position = 1 0 2\n  component mesh_reference\n"
     "    mesh = /meshes/rock\n    material = /materials/brick\n"},
};

void WriteImage(const io::Path& root, u8 seed)
{
    Vector<byte> pixels;
    for (u32 i = 0; i < 4 * 4 * 4; ++i)
    {
        pixels.Push(static_cast<byte>(i * 3 + seed));
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(4, 4, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Ok());
    const io::Path path = root / StringView("src/textures/brick.raw");
    REQUIRE(io::CreateDirectories(path.Parent()).Ok());
    REQUIRE(io::WriteFile(path, raw.Data(), raw.Size()).Ok());
}

// Writes the sample project to `root`; `with_ids` false leaves the `id` lines out.
void WriteSample(const io::Path& root, bool with_ids = true, u8 image_seed = 1)
{
    Put(root, "assets.project", "source_root src\noutput_root out\npak sample.pak\nexclude drafts\n");
    for (const SampleFile& f : kSample)
    {
        String<HeapAllocator> text(HeapAllocator::Default());
        if (with_ids)
        {
            text.Append(StringView("id "));
            text.Append(StringView(f.id));
            text.Append('\n');
        }
        text.Append(StringView(f.body));
        Put(root, f.rel, text.CStr());
    }
    Put(root, "src/drafts/skip.skel.src", "this is not a source and is excluded\n");
    WriteImage(root, image_seed);
}

Vector<u8> Read(const io::Path& path)
{
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    return bytes;
}

bool SameFile(const io::Path& a, const io::Path& b)
{
    const Vector<u8> x = Read(a);
    const Vector<u8> y = Read(b);
    return x.Size() == y.Size() && MemCompare(x.Data(), y.Data(), x.Size()) == 0;
}

struct Outcome
{
    Status status;
    MintStats stats;
    MintDiagnostics diagnostics;
};

Outcome MintProject(const io::Path& root, u32 workers = 1, bool force = false)
{
    Outcome o;
    Project project;
    o.status = project.Load(root / StringView("assets.project"), &o.diagnostics);
    if (o.status.Ok())
    {
        MintOptions options;
        options.workers = workers;
        options.force = force;
        o.status = project.Mint(options, &o.diagnostics, &o.stats);
    }
    return o;
}

const char* const kOutputs[] = {
    "out/assets.manifest",         "out/resources.table",          "out/skeletons/hero.skel.blob",
    "out/meshes/hero.mesh.blob",   "out/meshes/rock.mesh.blob",    "out/clips/idle.anim.blob",
    "out/textures/brick.tex.blob", "out/materials/brick.mat.blob", "out/levels/main.level.blob"};

}  // namespace

TEST_CASE("asset_mint: a sample project mints, a second run does nothing", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Outcome first = MintProject(dir.Path());
    REQUIRE(first.status.Ok());
    CHECK(first.stats.sources == 7);
    CHECK(first.stats.minted == 7);
    CHECK(first.stats.rewritten == 7);
    Outcome second = MintProject(dir.Path());
    REQUIRE(second.status.Ok());
    CHECK(second.stats.minted == 0);
    CHECK(second.stats.unchanged == 7);
    CHECK(second.stats.rewritten == 0);
}

TEST_CASE("asset_mint: the output is the same for 1 and 8 workers, after touching the sources and a forced mint",
          "[asset][mint][determinism]")
{
    test::TempDir one;
    test::TempDir eight;
    WriteSample(one.Path());
    WriteSample(eight.Path());
    REQUIRE(MintProject(one.Path(), 1).status.Ok());
    REQUIRE(MintProject(eight.Path(), 8).status.Ok());
    for (const char* rel : kOutputs)
    {
        INFO(rel);
        CHECK(SameFile(one.Path() / StringView(rel), eight.Path() / StringView(rel)));
    }
    // Pack both: the paks are the same bytes.
    for (test::TempDir* d : {&one, &eight})
    {
        Project project;
        MintDiagnostics diagnostics;
        REQUIRE(project.Load(d->Path() / StringView("assets.project"), &diagnostics).Ok());
        REQUIRE(project.Pack(&diagnostics).Ok());
    }
    CHECK(SameFile(one.Path() / StringView("out/sample.pak"), eight.Path() / StringView("out/sample.pak")));
    {
        // The pak holds the seven resources (a pak with none would be the same bytes for both, too).
        TypeRegistry types;
        io::FilePtr file;
        REQUIRE(
            io::File::Open(one.Path() / StringView("out/sample.pak"), io::OpenMode::Read, io::kOpenNone, &file).Ok());
        PakReader pak;
        REQUIRE(PakReader::Open(static_cast<io::FilePtr&&>(file), types, 0, &pak).Ok());
        CHECK(pak.Count() == 7);
    }

    // Touching every source (the same bytes written again: new modification times) changes nothing.
    WriteSample(one.Path());
    Outcome touched = MintProject(one.Path(), 4);
    REQUIRE(touched.status.Ok());
    CHECK(touched.stats.minted == 0);
    // A forced mint mints everything and writes nothing new.
    Outcome forced = MintProject(one.Path(), 3, true);
    REQUIRE(forced.status.Ok());
    CHECK(forced.stats.minted == 7);
    CHECK(forced.stats.rewritten == 0);
    for (const char* rel : kOutputs)
    {
        INFO(rel);
        CHECK(SameFile(one.Path() / StringView(rel), eight.Path() / StringView(rel)));
    }
}

TEST_CASE("asset_mint: a changed image re-mints the texture and what names it, and writes only the changed blob",
          "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    WriteImage(dir.Path(), 77);
    Outcome changed = MintProject(dir.Path());
    REQUIRE(changed.status.Ok());
    // The texture, and what reads its blob while minting (the material, and the level through the material): their
    // inputs changed, so they are minted again; the bytes of the material and the level hold ids only, so only the
    // texture's file is written.
    CHECK(changed.stats.minted == 3);
    CHECK(changed.stats.rewritten == 1);
    CHECK(changed.stats.unchanged == 4);
}

TEST_CASE("asset_mint: a changed skeleton re-mints the meshes and clips made for it", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    // Another joint: the skeleton's hash changes, which is part of a clip's blob and of a mesh's.
    Put(dir.Path(), "src/skeletons/hero.skel.src",
        "id 0000000000000a01\njoint root - 0 0 0  0 0 0 1  1 1 1\njoint arm root 0 1 0  0 0 0 1  1 1 1\n"
        "joint hand arm 0 1 0  0 0 0 1  1 1 1\n");
    Outcome o = MintProject(dir.Path());
    REQUIRE(o.status.Ok());
    CHECK(o.stats.minted ==
          3);  // the skeleton, the skinned mesh and the clip; the static mesh and the rest are untouched
    CHECK(o.stats.rewritten >= 2);
}

TEST_CASE("asset_mint: a moved source leaves no stale blob, and verify names blobs the table does not know",
          "[asset][mint][verify]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    // The same id under another path.
    REQUIRE(io::Remove(dir.Path() / StringView("src/meshes/rock.mesh.src")).Ok());
    Put(dir.Path(), "src/props/rock.mesh.src",
        "id 0000000000000b02\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n");
    // The level names the mesh by its old path: point it at the new one.
    Put(dir.Path(), "src/levels/main.level.src",
        "id 0000000000000f01\nentity rock\n  component local_transform\n    position = 1 0 2\n  component mesh_reference\n"
        "    mesh = /props/rock\n    material = /materials/brick\n");
    Outcome moved = MintProject(dir.Path());
    REQUIRE(moved.status.Ok());
    CHECK_FALSE(io::Exists(dir.Path() / StringView("out/meshes/rock.mesh.blob")));
    CHECK(io::Exists(dir.Path() / StringView("out/props/rock.mesh.blob")));
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    CHECK(project.Verify(&diagnostics).Ok());
    // A blob copied in by hand has the id of another one and no table entry.
    Vector<u8> bytes = Read(dir.Path() / StringView("out/props/rock.mesh.blob"));
    REQUIRE(io::CreateDirectories(dir.Path() / StringView("out/stray")).Ok());
    REQUIRE(io::WriteFile(dir.Path() / StringView("out/stray/rock.mesh.blob"), bytes.Data(), bytes.Size()).Ok());
    MintDiagnostics stray;
    CHECK(project.Verify(&stray).Failed());
    CHECK(stray.Mentions(StringView("stray/rock.mesh.blob")));
}

TEST_CASE("asset_mint: only references count, not words that are the names of other sources", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    // Each level has an entity named like the other level: not references.
    Put(dir.Path(), "src/levels/a.level.src",
        "id 0000000000000f10\nentity b\n  component local_transform\n    position = 1 0 2\n");
    Put(dir.Path(), "src/levels/b.level.src",
        "id 0000000000000f11\nentity a\n  component local_transform\n    position = 1 0 2\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Ok());
    CHECK(o.stats.failed == 0);
}

TEST_CASE("asset_mint: an include or a texture that names nothing is reported at load", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Put(dir.Path(), "src/levels/typo.level.src", "id 0000000000000f20\ninclude /levels/mian\n");
    Put(dir.Path(), "src/materials/bad.mat.src",
        "id 0000000000000e20\ntemplate gbuffer\ntexture albedo_tex /textures/missing\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Failed());
    CHECK(o.diagnostics.Mentions(StringView("levels/typo.level.src:2")));
    CHECK(o.diagnostics.Mentions(StringView("materials/bad.mat.src:3")));
    CHECK(o.stats.minted == 0);
}

TEST_CASE("asset_mint: an id written in capitals is an id, and an excluded folder may end in a slash", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Put(dir.Path(), "src/skeletons/hero.skel.src",
        "id 0000000000000A01\njoint root - 0 0 0  0 0 0 1  1 1 1\njoint arm root 0 1 0  0 0 0 1  1 1 1\n");
    Put(dir.Path(), "assets.project", "source_root src\noutput_root out\npak sample.pak\nexclude drafts/\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Ok());
    CHECK(o.stats.sources ==
          7);  // the drafts folder is excluded, the skeleton is found and the references to it resolve
    ProjectConfig config;
    ProjectError error;
    REQUIRE(ParseProjectConfig(StringView("source_root a\noutput_root b\nexclude x/\n"), &config, &error).Ok());
    CHECK(config.excludes[0].View() == StringView("x"));
    CHECK(ParseProjectConfig(StringView("source_root a\\b\noutput_root c\n"), &config, &error).Failed());
}

TEST_CASE("asset_mint: a manifest of another tool version mints everything again", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    Vector<u8> manifest = Read(dir.Path() / StringView("out/assets.manifest"));
    String<HeapAllocator> text(HeapAllocator::Default(),
                               StringView(reinterpret_cast<const char*>(manifest.Data()), manifest.Size()));
    const usize at = text.View().Find(StringView("tool 1"));
    REQUIRE(at != text.Length());
    String<HeapAllocator> edited(HeapAllocator::Default(), text.View().Substring(0, at));
    edited.Append(StringView("tool 999"));
    edited.Append(text.View().Substring(at + 6));
    REQUIRE(io::WriteFile(dir.Path() / StringView("out/assets.manifest"), edited.CStr(), edited.Length()).Ok());
    Outcome again = MintProject(dir.Path());
    REQUIRE(again.status.Ok());
    CHECK(again.stats.minted == 7);
    CHECK(again.stats.rewritten == 0);
}

TEST_CASE("asset_mint: a source with no id fails the mint naming adopt, and adopt is idempotent",
          "[asset][mint][adopt]")
{
    test::TempDir dir;
    WriteSample(dir.Path(), false);
    Outcome failed = MintProject(dir.Path());
    CHECK(failed.status.Failed());
    CHECK(failed.diagnostics.Mentions(StringView("asset_mint adopt")));
    CHECK(failed.diagnostics.ErrorCount() >= 7);  // every source is named, not only the first
    {
        Project project;
        MintDiagnostics diagnostics;
        (void)project.Load(dir.Path() / StringView("assets.project"), &diagnostics);
        u32 adopted = 0;
        REQUIRE(project.Adopt(&diagnostics, &adopted).Ok());
        CHECK(adopted == 7);
    }
    // The sources reference each other by id (`skeleton ...a01`), which adopt cannot know: the sample's references are
    // paths in the level and the material, and the ids of the mesh and clip are written by hand below.
    Vector<u8> before = Read(dir.Path() / StringView("src/skeletons/hero.skel.src"));
    {
        Project project;
        MintDiagnostics diagnostics;
        (void)project.Load(dir.Path() / StringView("assets.project"), &diagnostics);
        u32 adopted = 99;
        REQUIRE(project.Adopt(&diagnostics, &adopted).Ok());
        CHECK(adopted == 0);
    }
    Vector<u8> after = Read(dir.Path() / StringView("src/skeletons/hero.skel.src"));
    CHECK(before.Size() == after.Size());
    CHECK(MemCompare(before.Data(), after.Data(), before.Size()) == 0);
}

TEST_CASE("asset_mint: two sources with one id are named together", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Put(dir.Path(), "src/meshes/rock_copy.mesh.src",
        "id 0000000000000b02\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Failed());
    CHECK(o.diagnostics.Mentions(StringView("meshes/rock.mesh.src")));
    CHECK(o.diagnostics.Mentions(StringView("meshes/rock_copy.mesh.src")));
    CHECK(o.diagnostics.Mentions(StringView("reid")));
}

TEST_CASE("asset_mint: a source deleted by hand makes the mint fail naming its dependents", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    REQUIRE(io::Remove(dir.Path() / StringView("src/skeletons/hero.skel.src")).Ok());
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Failed());
    CHECK(o.diagnostics.Mentions(StringView("meshes/hero.mesh.src")));
    CHECK(o.diagnostics.Mentions(StringView("clips/idle.anim.src")));
    CHECK(o.stats.minted == 0);  // nothing is minted while the project has errors
    // A texture named on a `texture` line is found at load; a mesh named by a level's field is found through the
    // manifest, which remembers who used what.
    test::TempDir other;
    WriteSample(other.Path());
    REQUIRE(MintProject(other.Path()).status.Ok());
    REQUIRE(io::Remove(other.Path() / StringView("src/textures/brick.tex.src")).Ok());
    Outcome texture = MintProject(other.Path());
    CHECK(texture.status.Failed());
    CHECK(texture.diagnostics.Mentions(StringView("materials/brick.mat.src")));
    test::TempDir third;
    WriteSample(third.Path());
    REQUIRE(MintProject(third.Path()).status.Ok());
    REQUIRE(io::Remove(third.Path() / StringView("src/meshes/rock.mesh.src")).Ok());
    Outcome by_path = MintProject(third.Path());
    CHECK(by_path.status.Failed());
    CHECK(by_path.diagnostics.Mentions(StringView("levels/main.level.src")));
    CHECK(by_path.stats.failed == 1);
    CHECK(by_path.stats.unchanged == 5);  // what does not depend on it is untouched
}

TEST_CASE("asset_mint: an error in one file does not stop the others and names the file and line", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Put(dir.Path(), "src/meshes/broken.mesh.src", "id 0000000000000bad\nbogus record\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Failed());
    CHECK(o.stats.failed == 1);
    CHECK(o.stats.minted == 7);
    CHECK(o.diagnostics.Mentions(StringView("meshes/broken.mesh.src:2")));
}

TEST_CASE("asset_mint: references that form a cycle are named", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    Put(dir.Path(), "src/levels/a.level.src", "id 0000000000000f10\ninclude /levels/b\n");
    Put(dir.Path(), "src/levels/b.level.src", "id 0000000000000f11\ninclude /levels/a\n");
    Outcome o = MintProject(dir.Path());
    CHECK(o.status.Failed());
    CHECK(o.diagnostics.Mentions(StringView("cycle")));
    CHECK(o.diagnostics.Mentions(StringView("levels/a.level.src")));
    CHECK(o.diagnostics.Mentions(StringView("levels/b.level.src")));
}

TEST_CASE("asset_mint: verify passes, and fails for an edited blob, naming it", "[asset][mint][verify]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    REQUIRE(MintProject(dir.Path()).status.Ok());
    Project project;
    MintDiagnostics loaded;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &loaded).Ok());
    MintDiagnostics fine;
    CHECK(project.Verify(&fine).Ok());
    REQUIRE(project.Pack(&fine).Ok());
    CHECK(project.Verify(&fine).Ok());
    // Flip one byte of the material blob (its dependency section is among the bytes the hash covers).
    const io::Path blob = dir.Path() / StringView("out/materials/brick.mat.blob");
    Vector<u8> bytes = Read(blob);
    bytes[bytes.Size() - 1] ^= 0x5a;
    REQUIRE(io::WriteFile(blob, bytes.Data(), bytes.Size()).Ok());
    MintDiagnostics broken;
    CHECK(project.Verify(&broken).Failed());
    CHECK(broken.Mentions(StringView("materials/brick.mat.blob")));
    // And a blob that went missing breaks the closure of what names it.
    REQUIRE(io::Remove(dir.Path() / StringView("out/textures/brick.tex.blob")).Ok());
    MintDiagnostics missing;
    CHECK(project.Verify(&missing).Failed());
}

TEST_CASE("asset_mint: a decomposed file name becomes the normalized resource path", "[asset][mint]")
{
    test::TempDir dir;
    WriteSample(dir.Path());
    // `e` and a combining acute accent: not NFC.
    Put(dir.Path(), "src/meshes/cafe\xcc\x81.mesh.src",
        "id 0000000000000b77\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n");
    REQUIRE(MintProject(dir.Path()).status.Ok());
    Project project;
    MintDiagnostics diagnostics;
    REQUIRE(project.Load(dir.Path() / StringView("assets.project"), &diagnostics).Ok());
    String<HeapAllocator> listing(HeapAllocator::Default());
    REQUIRE(project.List(false, &diagnostics, &listing).Ok());
    CHECK(listing.View().Contains(StringView("/meshes/caf\xc3\xa9")));
}

TEST_CASE("project file: records, rules and line-numbered errors", "[asset][mint]")
{
    ProjectConfig config;
    ProjectError error;
    REQUIRE(ParseProjectConfig(StringView("source_root a\noutput_root b\ntarget 3\nexclude x/y\nrule .tex.src skip\n"),
                               &config, &error)
                .Ok());
    CHECK(config.target == 3);
    CHECK(config.excludes.Size() == 1);
    CHECK(config.skipped_extensions.Size() == 1);
    CHECK(config.pak.View() == StringView("assets.pak"));
    CHECK(ParseProjectConfig(StringView("source_root a\noutput_root b\nbogus 1\n"), &config, &error).Failed());
    CHECK(error.line == 3);
    CHECK(ParseProjectConfig(StringView("source_root /abs\noutput_root b\n"), &config, &error).Failed());
    CHECK(error.line == 1);
    CHECK(ParseProjectConfig(StringView("source_root ../up\noutput_root b\n"), &config, &error).Failed());
    CHECK(ParseProjectConfig(StringView("source_root a\n"), &config, &error).Failed());
    CHECK(error.line == 0);
    CHECK(ParseProjectConfig(StringView("source_root a\nsource_root b\noutput_root c\n"), &config, &error).Failed());
    CHECK(error.line == 2);
}

}  // namespace vse::asset

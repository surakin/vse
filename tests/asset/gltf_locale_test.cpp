// the host maintenance (2026-11-18, plans/maintenance/host-locale-independent-import.md): a host that sets its
// locale from the environment (Qt, GTK) runs the importer under a decimal-comma locale, where printf's %g writes "0,5".
// The import must succeed there and mint the same bytes, summary and notes as under the C locale.

#include <catch2/catch_test_macros.hpp>

#include "asset/material.h"
#include "asset_import/gltf_import.h"
#include "asset_import/material_source.h"
#include "comma_locale.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "gltf_writer.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;

// A glb with fractional material factors, a node with a fractional translation and an animation: the material text
// and the summary's numbers. (The resampling note of a joint's cubic spline is checked in gltf_skin_test.cpp.)
Vector<byte> FractionalGlb()
{
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const f32 times[3] = {0, 1, 3};
    // in tangent, value, out tangent per key
    const f32 spline[27] = {0, 0, 0, 0, 0, 0, 4, 0, 0,  //
                            4, 0, 0, 3, 1, 0, 0, 2, 0,  //
                            0, 0, 0, 5, 0, 0, 0, 0, 0};
    const u32 pos = w.AddAccessor(w.AddView(positions, sizeof(positions)), 5126, 3, "VEC3");
    const u32 idx = w.AddAccessor(w.AddView(indices, sizeof(indices)), 5123, 3, "SCALAR");
    const u32 time = w.AddAccessor(w.AddView(times, sizeof(times)), 5126, 3, "SCALAR");
    const u32 values = w.AddAccessor(w.AddView(spline, sizeof(spline)), 5126, 9, "VEC3");
    char json[1600];
    FormatTo(
        json, sizeof(json),
        "\"materials\":[{\"name\":\"grass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,0.125,1],"
        "\"metallicFactor\":0.1,\"roughnessFactor\":0.75},\"emissiveFactor\":[0.5,0.5,0.5]},"
        "{\"name\":\"leaf\",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3}],"
        "\"meshes\":[{\"name\":\"m\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u,"
        "\"material\":0},{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":1}]}],"
        "\"nodes\":[{\"name\":\"robot\",\"mesh\":0,\"translation\":[0.5,1.25,-2.75]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0,"
        "\"animations\":[{\"name\":\"bob\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,"
        "\"path\":\"translation\"}}],\"samplers\":[{\"input\":%u,\"output\":%u,\"interpolation\":\"CUBICSPLINE\"}]}]",
        pos, idx, pos, idx, time, values);
    w.AddRoot(json);
    return w.Glb();
}

Status LcImport(const Vector<byte>& glb, ImportResult* result)
{
    NoGltfSource none;
    SeededIdSource ids(53);
    ImportOptions options;
    options.ids = &ids;
    options.emit_level = true;
    GltfError error;
    const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, result, &error);
    if (status.Failed())
    {
        UNSCOPED_INFO("ImportGltf: " << GltfMessage(error, status));
    }
    return status;
}

bool SameBytes(const Vector<byte>& a, const Vector<byte>& b)
{
    return a.Size() == b.Size() && (a.Size() == 0 || MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

bool NoPath(void*, ResourceId, String<HeapAllocator>*)
{
    return false;
}

}  // namespace

TEST_CASE("gltf locale: an import under a decimal-comma locale succeeds and mints what the C locale mints",
          "[asset][gltf][locale]")
{
    const Vector<byte> glb = FractionalGlb();
    ImportResult in_c;
    REQUIRE(LcImport(glb, &in_c).Ok());
    INFO(in_c.summary.CStr());
    CHECK(in_c.summary.View().Contains(StringView("[0.5,1.25,-2.75]")));

    ::engine_placeholder::test::CommaLocale comma;
    if (!comma.Active())
    {
        SKIP("no decimal-comma locale is installed (tried nb_NO, de_DE and fr_FR)");
    }
    INFO("locale " << comma.Name());
    ImportResult in_comma;
    REQUIRE(LcImport(glb, &in_comma).Ok());  // the bug: "materials[0]: a float value is a finite number"

    REQUIRE(in_comma.resources.Size() == in_c.resources.Size());
    for (usize i = 0; i < in_c.resources.Size(); ++i)
    {
        INFO("resource " << in_c.resources[i].path.CStr());
        CHECK(in_comma.resources[i].type == in_c.resources[i].type);
        CHECK(in_comma.resources[i].path.View() == in_c.resources[i].path.View());
        CHECK(SameBytes(in_comma.resources[i].blob, in_c.resources[i].blob));
    }
    CHECK(in_comma.summary.View() == in_c.summary.View());
    REQUIRE(in_comma.report.notes.Size() == in_c.report.notes.Size());
    for (usize i = 0; i < in_c.report.notes.Size(); ++i)
    {
        CHECK(in_comma.report.notes[i].View() == in_c.report.notes[i].View());
    }
}

TEST_CASE("gltf locale: a material blob prints the same text under a decimal-comma locale, and it mints back",
          "[asset][gltf][locale]")
{
    const Vector<byte> glb = FractionalGlb();
    ImportResult result;
    REQUIRE(LcImport(glb, &result).Ok());
    const ImportedResource* material = nullptr;
    for (usize i = 0; i < result.resources.Size() && material == nullptr; ++i)
    {
        if (result.resources[i].type == types::kMaterial)
        {
            material = &result.resources[i];
        }
    }
    REQUIRE(material != nullptr);
    TypeRegistry types;
    const ByteSpan blob(material->blob.Data(), material->blob.Size());
    String<HeapAllocator> in_c(HeapAllocator::Default());
    MaterialSourceError error;
    REQUIRE(FormatMaterialBlob(blob, types, NoPath, nullptr, &in_c, &error).Ok());
    INFO(in_c.CStr());
    CHECK(in_c.View().Contains(StringView("param roughness float 0.75")));

    ::engine_placeholder::test::CommaLocale comma;
    if (!comma.Active())
    {
        SKIP("no decimal-comma locale is installed (tried nb_NO, de_DE and fr_FR)");
    }
    String<HeapAllocator> in_comma(HeapAllocator::Default());
    REQUIRE(FormatMaterialBlob(blob, types, NoPath, nullptr, &in_comma, &error).Ok());
    CHECK(in_comma.View() == in_c.View());
}

}  // namespace engine_placeholder::asset

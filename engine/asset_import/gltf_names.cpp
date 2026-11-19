#include "asset_import/gltf_names.h"
#include "containers/hash.h"

#include "algorithm.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr usize kMaxNotes = 256;
// Each pass is n log n; a chain of planted names (`a`, `a`, `a_1`, `a_1_2`, ...) settles one link per pass, so the
// quadratic fallback below is reached only by a chain longer than this.
constexpr u32 kMaxPasses = 32;

u64 NameHash(StringView name)
{
    return Fnv1a64Builder(kFnv1a64LegacyBasis).AddText(name).Value();
}

struct HashedName
{
    u64 hash;
    u32 position;
};

bool HashedNameLess(const HashedName& a, const HashedName& b)
{
    return a.hash != b.hash ? a.hash < b.hash : a.position < b.position;
}

}  // namespace

// A name seen before gets `_<position>` appended, for up to `kMaxPasses` passes (the rare clash with another name needs
// more); what is left is settled one by one against the names before it, so the result is always distinct.
void MakeNamesUnique(Vector<String<HeapAllocator>>* names)
{
    for (u32 pass = 0; pass < kMaxPasses; ++pass)
    {
        Vector<HashedName> keys;
        for (usize i = 0; i < names->Size(); ++i)
        {
            keys.Push(HashedName{NameHash((*names)[i].View()), static_cast<u32>(i)});
        }
        Sort(Span<HashedName>(keys.Data(), keys.Size()), &HashedNameLess);
        bool changed = false;
        Vector<u32> firsts;  // in a run of one hash: the first position of each distinct name
        for (usize i = 0; i < keys.Size(); ++i)
        {
            if (i == 0 || keys[i].hash != keys[i - 1].hash)
            {
                firsts.Clear();
            }
            bool seen = false;
            for (usize f = 0; f < firsts.Size(); ++f)
            {
                seen = seen || (*names)[firsts[f]].View() == (*names)[keys[i].position].View();
            }
            if (!seen)
            {
                firsts.Push(keys[i].position);
                continue;
            }
            char suffix[16];
            FormatTo(suffix, sizeof(suffix), "_%u", static_cast<unsigned>(keys[i].position));
            (*names)[keys[i].position].Append(StringView(suffix, CStrLength(suffix)));
            changed = true;
        }
        if (!changed)
        {
            return;
        }
    }
    for (usize i = 1; i < names->Size(); ++i)
    {
        for (usize k = 0; k < i; ++k)
        {
            if ((*names)[k].View() == (*names)[i].View())
            {
                (*names)[i].Append('_');
                k = static_cast<usize>(-1);  // compare with the names before it again
            }
        }
    }
}

void CleanPathSegment(StringView name, String<HeapAllocator>* out)
{
    *out = String<HeapAllocator>(HeapAllocator::Default());
    for (usize i = 0; i < name.Length(); ++i)
    {
        const u8 c = static_cast<u8>(name[i]);
        out->Append((c < 0x20 || c == 0x7f || c == '/' || c == '\\') ? '_' : static_cast<char>(c));
    }
    if (out->View() == StringView(".") || out->View() == StringView(".."))
    {
        *out = String<HeapAllocator>(HeapAllocator::Default(), StringView("_"));
    }
}

void AddImportWarning(ImportReport* report, StringView where, StringView what)
{
    report->warnings += 1;
    if (report->notes.Size() >= kMaxNotes)
    {
        return;
    }
    String<HeapAllocator> note(HeapAllocator::Default(), where);
    if (where.Length() != 0)
    {
        note.Append(StringView(": "));
    }
    note.Append(what);
    report->notes.Push(static_cast<String<HeapAllocator>&&>(note));
}

void AddImportNote(ImportReport* report, StringView where, StringView what)
{
    if (report->notes.Size() >= kMaxNotes)
    {
        return;
    }
    String<HeapAllocator> note(HeapAllocator::Default(), where);
    if (where.Length() != 0)
    {
        note.Append(StringView(": "));
    }
    note.Append(what);
    report->notes.Push(static_cast<String<HeapAllocator>&&>(note));
}

String<HeapAllocator> ImportPathPrefix(const ImportOptions& options)
{
    String<HeapAllocator> prefix(HeapAllocator::Default(),
                                 options.path_prefix.Length() != 0 ? options.path_prefix : StringView("/gltf"));
    while (prefix.Length() > 1 && prefix.View()[prefix.Length() - 1] == '/')
    {
        prefix = String<HeapAllocator>(HeapAllocator::Default(), prefix.View().Substring(0, prefix.Length() - 1));
    }
    return prefix;
}

void AssignIds(const ImportOptions& options, ImportKind kind, Span<const ManifestObject> objects,
               Vector<ResourceId>* ids)
{
    ids->Clear();
    if (options.manifest != nullptr)
    {
        options.manifest->Match(kind, objects, ids);
    }
    while (ids->Size() < objects.Size())
    {
        ids->Push(ResourceId());
    }
    RandomIdSource random_ids;
    IdSource& source = options.ids != nullptr ? *options.ids : static_cast<IdSource&>(random_ids);
    for (usize i = 0; i < objects.Size(); ++i)
    {
        if ((*ids)[i].IsNone())
        {
            (*ids)[i] = source.Next();
        }
    }
}

}  // namespace engine_placeholder::asset

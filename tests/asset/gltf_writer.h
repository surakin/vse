#pragma once

// A minimal glTF and GLB writer for the importer's tests (Phase 22 Day 335; Days 336 to 338 extend it): the files the
// importer reads are written here, never downloaded.

#include "asset_import/gltf_import.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "string/string.h"
#include "support.h"

namespace engine_placeholder::asset::test
{

inline void AppendText(Vector<byte>* out, StringView text)
{
    for (usize i = 0; i < text.Length(); ++i)
    {
        out->Push(static_cast<byte>(text[i]));
    }
}

inline void Put32(Vector<byte>* out, u32 value)
{
    for (u32 i = 0; i < 4; ++i)
    {
        out->Push(static_cast<byte>(value >> (8 * i)));
    }
}

// Builds one buffer and the views and accessors over it.
class GltfWriter
{
public:
    GltfWriter()
        : views_(HeapAllocator::Default()), accessors_(HeapAllocator::Default()), extra_(HeapAllocator::Default())
    {
    }

    // Appends bytes (padded to four) as a buffer view; returns the view's index.
    u32 AddView(const void* data, usize size, u32 stride = 0)
    {
        while (bin.Size() % 4 != 0)
        {
            bin.Push(0);
        }
        const usize offset = bin.Size();
        for (usize i = 0; i < size; ++i)
        {
            bin.Push(static_cast<const byte*>(data)[i]);
        }
        char text[160];
        if (stride != 0)
        {
            FormatTo(text, sizeof(text), "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"byteStride\":%u}",
                     view_count_ == 0 ? "" : ",", static_cast<unsigned>(offset), static_cast<unsigned>(size),
                     static_cast<unsigned>(stride));
        }
        else
        {
            FormatTo(text, sizeof(text), "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
                     view_count_ == 0 ? "" : ",", static_cast<unsigned>(offset), static_cast<unsigned>(size));
        }
        views_.Append(StringView(text, CStrLength(text)));
        return view_count_++;
    }

    // An accessor over a view; `extra` is more JSON members (`,"normalized":true`, a sparse object).
    u32 AddAccessor(i64 view, u32 component_type, u32 count, const char* type, const char* extra = "",
                    u32 byte_offset = 0)
    {
        char text[512];
        FormatTo(text, sizeof(text), "%s{%s\"componentType\":%u,\"count\":%u,\"type\":\"%s\"%s%s}",
                 accessor_count_ == 0 ? "" : ",", view >= 0 ? (std_view(view)) : "",
                 static_cast<unsigned>(component_type), static_cast<unsigned>(count), type,
                 byte_offset != 0 ? (std_offset(byte_offset)) : "", extra);
        accessors_.Append(StringView(text, CStrLength(text)));
        return accessor_count_++;
    }

    // Free-form members of the root object (`"meshes":[...]`), comma-separated by the caller.
    void AddRoot(const char* json)
    {
        if (extra_.Length() != 0)
        {
            extra_.Append(',');
        }
        extra_.Append(StringView(json, CStrLength(json)));
    }

    // The JSON text of the whole file; `buffer_uri` is the uri of buffer 0 (null: none, for a .glb).
    String<HeapAllocator> Json(const char* buffer_uri = nullptr, bool with_asset = true,
                               const char* version = "2.0") const
    {
        String<HeapAllocator> out(HeapAllocator::Default());
        out.Append('{');
        if (with_asset)
        {
            char text[64];
            FormatTo(text, sizeof(text), "\"asset\":{\"version\":\"%s\"},", version);
            out.Append(StringView(text, CStrLength(text)));
        }
        char buffer[4096];
        if (buffer_uri != nullptr)
        {
            FormatTo(buffer, sizeof(buffer), "\"buffers\":[{\"byteLength\":%u,\"uri\":\"%s\"}]",
                     static_cast<unsigned>(bin.Size()), buffer_uri);
        }
        else
        {
            FormatTo(buffer, sizeof(buffer), "\"buffers\":[{\"byteLength\":%u}]", static_cast<unsigned>(bin.Size()));
        }
        out.Append(StringView(buffer, CStrLength(buffer)));
        out.Append(StringView(",\"bufferViews\":["));
        out.Append(views_.View());
        out.Append(StringView("],\"accessors\":["));
        out.Append(accessors_.View());
        out.Append(']');
        if (extra_.Length() != 0)
        {
            out.Append(',');
            out.Append(extra_.View());
        }
        out.Append('}');
        return out;
    }

    // The bytes of the buffer as a base64 data: URI.
    String<HeapAllocator> DataUri() const
    {
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String<HeapAllocator> out(HeapAllocator::Default(), StringView("data:application/octet-stream;base64,"));
        for (usize i = 0; i < bin.Size(); i += 3)
        {
            const usize remain = bin.Size() - i;
            const u32 word = static_cast<u32>(bin[i]) << 16 | (remain > 1 ? static_cast<u32>(bin[i + 1]) << 8 : 0) |
                             (remain > 2 ? static_cast<u32>(bin[i + 2]) : 0);
            out.Append(alphabet[(word >> 18) & 63]);
            out.Append(alphabet[(word >> 12) & 63]);
            out.Append(remain > 1 ? alphabet[(word >> 6) & 63] : '=');
            out.Append(remain > 2 ? alphabet[word & 63] : '=');
        }
        return out;
    }

    // The JSON of a .gltf whose buffer is inside it (a data: URI).
    String<HeapAllocator> JsonInline() const
    {
        const String<HeapAllocator> uri = DataUri();
        return Json(uri.CStr());
    }

    // A .glb: the header, the JSON chunk (padded with spaces) and the BIN chunk (padded with zeros).
    Vector<byte> Glb() const
    {
        const String<HeapAllocator> json = Json();
        Vector<byte> json_chunk;
        AppendText(&json_chunk, json.View());
        while (json_chunk.Size() % 4 != 0)
        {
            json_chunk.Push(' ');
        }
        Vector<byte> bin_chunk;
        for (usize i = 0; i < bin.Size(); ++i)
        {
            bin_chunk.Push(bin[i]);
        }
        while (bin_chunk.Size() % 4 != 0)
        {
            bin_chunk.Push(0);
        }
        Vector<byte> out;
        Put32(&out, 0x46546C67);
        Put32(&out, 2);
        Put32(&out, static_cast<u32>(12 + 8 + json_chunk.Size() + 8 + bin_chunk.Size()));
        Put32(&out, static_cast<u32>(json_chunk.Size()));
        Put32(&out, 0x4E4F534A);
        for (usize i = 0; i < json_chunk.Size(); ++i)
        {
            out.Push(json_chunk[i]);
        }
        Put32(&out, static_cast<u32>(bin_chunk.Size()));
        Put32(&out, 0x004E4942);
        for (usize i = 0; i < bin_chunk.Size(); ++i)
        {
            out.Push(bin_chunk[i]);
        }
        return out;
    }

    Vector<byte> bin;

private:
    static const char* std_view(i64 view)
    {
        static char text[48];
        FormatTo(text, sizeof(text), "\"bufferView\":%d,", static_cast<int>(view));
        return text;
    }
    static const char* std_offset(u32 offset)
    {
        static char text[48];
        FormatTo(text, sizeof(text), ",\"byteOffset\":%u", static_cast<unsigned>(offset));
        return text;
    }

    String<HeapAllocator> views_;
    String<HeapAllocator> accessors_;
    String<HeapAllocator> extra_;
    u32 view_count_ = 0;
    u32 accessor_count_ = 0;
};

inline ByteSpan SpanOfText(const String<HeapAllocator>& text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text.CStr()), text.Length());
}

// A source with named files.
class TestSource final : public GltfSource
{
public:
    void Add(const char* name, const Vector<byte>& bytes)
    {
        names.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(name)));
        files.Push(Vector<byte>());
        Vector<byte>& file = files[files.Size() - 1];
        for (usize i = 0; i < bytes.Size(); ++i)
        {
            file.Push(bytes[i]);
        }
    }
    Status Read(StringView name, u64 max_bytes, Vector<byte>* out) override
    {
        reads += 1;
        last_max = max_bytes;
        last = String<HeapAllocator>(HeapAllocator::Default(), name);
        for (usize i = 0; i < names.Size(); ++i)
        {
            if (names[i].View() == name)
            {
                const usize size = files[i].Size() < max_bytes ? files[i].Size() : static_cast<usize>(max_bytes);
                out->Resize(size);
                MemCopy(out->Data(), files[i].Data(), size);
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such file");
    }
    Vector<String<HeapAllocator>> names;
    Vector<Vector<byte>> files;
    u32 reads = 0;
    u64 last_max = 0;
    String<HeapAllocator> last{HeapAllocator::Default()};
};

}  // namespace engine_placeholder::asset::test

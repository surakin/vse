#include "asset/script_blob.h"

#include "asset/content_hash.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{

namespace
{

void StoreScriptU32(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
    p[2] = static_cast<byte>((value >> 16) & 0xFF);
    p[3] = static_cast<byte>((value >> 24) & 0xFF);
}

u32 LoadScriptU32(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

u64 ScriptSourceHash(ByteSpan source)
{
    ContentHasher hasher;
    hasher.Update(source);
    const ContentHash hash = hasher.Finish();
    u64 value = 0;
    for (usize i = 0; i < 8; ++i)
    {
        value |= static_cast<u64>(hash.bytes[i]) << (8 * i);
    }
    return value;
}

void ScriptAppend(Vector<byte>* out, ByteSpan bytes)
{
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        out->Push(bytes[i]);
    }
}

void ScriptAppendU32(Vector<byte>* out, u32 value)
{
    byte b[4];
    StoreScriptU32(b, value);
    ScriptAppend(out, ByteSpan(b, 4));
}

ByteSpan TextBytes(const String<HeapAllocator>& text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text.CStr()), text.Length());
}

}  // namespace

Status BuildScriptBlob(const TypeRegistry& types, ResourceId id, const ScriptBuild& build, Vector<byte>* out,
                       u32 target)
{
    if (id.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "a script needs an id");
    }
    if (build.source.Length() == 0 || build.source.Length() > kMaxScriptSourceBytes)
    {
        return Status::Make(ErrorCode::Invalid, "the script source is empty or above the limit");
    }
    if (build.language.Length() == 0 || build.language.Length() > kMaxScriptTextBytes ||
        build.language_version.Length() > kMaxScriptTextBytes || build.bytecode_key.Length() > kMaxScriptTextBytes)
    {
        return Status::Make(ErrorCode::Invalid, "a language, version or bytecode key name is empty or too long");
    }
    if (build.entries.Size() > kMaxScriptEntries)
    {
        return Status::Make(ErrorCode::Invalid, "the script lists too many entry points");
    }
    if (build.bytecode.Size() > kMaxScriptBytecodeBytes ||
        (build.bytecode.Size() == 0) != (build.bytecode_key.Length() == 0))
    {
        return Status::Make(ErrorCode::Invalid, "the bytecode is too large, or it and its key are not both given");
    }
    for (usize i = 0; i < build.dependencies.Size(); ++i)
    {
        if (build.dependencies[i].IsNone() || build.dependencies[i] == id ||
            (i > 0 && !(build.dependencies[i - 1] < build.dependencies[i])))
        {
            return Status::Make(ErrorCode::Invalid,
                                "the dependencies must be strictly increasing, not none and not the script itself");
        }
    }
    Vector<byte> entries;
    for (usize i = 0; i < build.entries.Size(); ++i)
    {
        const String<HeapAllocator>& name = build.entries[i];
        if (name.Length() == 0 || name.Length() > kMaxScriptEntryBytes)
        {
            return Status::Make(ErrorCode::Invalid, "an entry point name is empty or too long");
        }
        ScriptAppendU32(&entries, static_cast<u32>(name.Length()));
        ScriptAppend(&entries, TextBytes(name));
    }
    byte head[kScriptHeaderBytes];
    MemSet(head, 0, sizeof(head));
    const ByteSpan source = TextBytes(build.source);
    const u64 hash = ScriptSourceHash(source);
    StoreScriptU32(head, static_cast<u32>(build.source.Length()));
    StoreScriptU32(head + 4, static_cast<u32>(build.entries.Size()));
    StoreScriptU32(head + 8, static_cast<u32>(hash & 0xFFFFFFFFull));
    StoreScriptU32(head + 12, static_cast<u32>(hash >> 32));
    StoreScriptU32(head + 16, static_cast<u32>(build.language.Length()));
    StoreScriptU32(head + 20, static_cast<u32>(build.language_version.Length()));
    Vector<byte> language;
    ScriptAppend(&language, TextBytes(build.language));
    ScriptAppend(&language, TextBytes(build.language_version));
    BlobWriter writer(types, types::kScript, id, target);
    for (usize i = 0; i < build.dependencies.Size(); ++i)
    {
        Dependency d;
        d.id = build.dependencies[i];
        d.type = types::kScript;
        d.type_version = 1;
        d.role = 0;
        writer.AddDependency(d);
    }
    ENGINE_TRY(writer.AddSection(kScriptSectionHeader, ByteSpan(head, sizeof(head))));
    ENGINE_TRY(writer.AddSection(kScriptSectionSource, source));
    ENGINE_TRY(writer.AddSection(kScriptSectionLanguage, ByteSpan(language.Data(), language.Size())));
    ENGINE_TRY(writer.AddSection(kScriptSectionEntries, ByteSpan(entries.Data(), entries.Size())));
    if (build.bytecode.Size() != 0)
    {
        Vector<byte> code;
        ScriptAppendU32(&code, static_cast<u32>(build.bytecode_key.Length()));
        ScriptAppend(&code, TextBytes(build.bytecode_key));
        ScriptAppend(&code, ByteSpan(build.bytecode.Data(), build.bytecode.Size()));
        ENGINE_TRY(writer.AddSection(kScriptSectionBytecode, ByteSpan(code.Data(), code.Size())));
    }
    return writer.Finish(out);
}

Status ScriptView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = ScriptView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kScript)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a script");
    }
    ByteSpan head;
    if (!blob.FindSection(kScriptSectionHeader, &head) || head.Size() != kScriptHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the script header section is missing or has the wrong size");
    }
    const u32 source_bytes = LoadScriptU32(head.Data());
    const u32 entry_count = LoadScriptU32(head.Data() + 4);
    const u64 hash = static_cast<u64>(LoadScriptU32(head.Data() + 8)) |
                     (static_cast<u64>(LoadScriptU32(head.Data() + 12)) << 32);
    const u32 language_bytes = LoadScriptU32(head.Data() + 16);
    const u32 version_bytes = LoadScriptU32(head.Data() + 20);
    if (LoadScriptU32(head.Data() + 24) != 0 || LoadScriptU32(head.Data() + 28) != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "a script header field holds a value this version does not define");
    }
    if (source_bytes == 0 || source_bytes > kMaxScriptSourceBytes || entry_count > kMaxScriptEntries ||
        language_bytes == 0 || language_bytes > kMaxScriptTextBytes || version_bytes > kMaxScriptTextBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "a script header field is out of range");
    }
    ByteSpan source;
    if (!blob.FindSection(kScriptSectionSource, &source) || source.Size() != source_bytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the script source section is missing or has the wrong size");
    }
    ByteSpan language;
    if (!blob.FindSection(kScriptSectionLanguage, &language) ||
        language.Size() != static_cast<usize>(language_bytes) + version_bytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the script language section is missing or has the wrong size");
    }
    ByteSpan entries;
    if (!blob.FindSection(kScriptSectionEntries, &entries))
    {
        return Status::Make(ErrorCode::Corrupt, "the script entry section is missing");
    }
    usize at = 0;
    for (u32 i = 0; i < entry_count; ++i)
    {
        if (entries.Size() - at < 4)
        {
            return Status::Make(ErrorCode::Corrupt, "the script entry list is truncated");
        }
        const u32 length = LoadScriptU32(entries.Data() + at);
        at += 4;
        if (length == 0 || length > kMaxScriptEntryBytes || entries.Size() - at < length)
        {
            return Status::Make(ErrorCode::Corrupt, "a script entry name is empty, too long or truncated");
        }
        at += length;
    }
    if (at != entries.Size())
    {
        return Status::Make(ErrorCode::Corrupt, "the script entry list has bytes after its last entry");
    }
    ByteSpan code;
    if (blob.FindSection(kScriptSectionBytecode, &code))
    {
        if (code.Size() < 5)
        {
            return Status::Make(ErrorCode::Corrupt, "the script bytecode section is malformed");
        }
        const u32 key = LoadScriptU32(code.Data());
        if (key == 0 || key > kMaxScriptTextBytes || code.Size() - 4 <= key ||
            code.Size() - 4 - key > kMaxScriptBytecodeBytes)
        {
            return Status::Make(ErrorCode::Corrupt, "the script bytecode section is malformed");
        }
        key_size_ = key;
        bytecode_offset_ = static_cast<usize>(code.Data() - data.Data());
        bytecode_size_ = code.Size();
    }
    id_ = blob.Id();
    bytes_ = bytes;
    source_offset_ = static_cast<usize>(source.Data() - data.Data());
    source_size_ = source.Size();
    source_hash_ = hash;
    language_offset_ = static_cast<usize>(language.Data() - data.Data());
    language_size_ = language_bytes;
    version_size_ = version_bytes;
    entries_offset_ = static_cast<usize>(entries.Data() - data.Data());
    entries_size_ = entries.Size();
    entry_count_ = entry_count;
    return Status::Success();
}

ByteSpan ScriptView::Source() const
{
    return bytes_ ? ByteSpan(bytes_->Bytes().Data() + source_offset_, source_size_) : ByteSpan();
}

StringView ScriptView::Language() const
{
    return bytes_ ? StringView(reinterpret_cast<const char*>(bytes_->Bytes().Data()) + language_offset_, language_size_)
                  : StringView();
}

StringView ScriptView::LanguageVersion() const
{
    return bytes_
               ? StringView(reinterpret_cast<const char*>(bytes_->Bytes().Data()) + language_offset_ + language_size_,
                            version_size_)
               : StringView();
}

StringView ScriptView::Entry(usize index) const
{
    if (!bytes_ || index >= entry_count_)
    {
        return StringView();
    }
    const byte* base = bytes_->Bytes().Data() + entries_offset_;
    usize at = 0;
    for (usize i = 0; i < index; ++i)
    {
        at += 4 + LoadScriptU32(base + at);
    }
    return StringView(reinterpret_cast<const char*>(base + at + 4), LoadScriptU32(base + at));
}

ByteSpan ScriptView::BytecodeFor(StringView key) const
{
    if (!bytes_ || bytecode_size_ == 0)
    {
        return ByteSpan();
    }
    const byte* base = bytes_->Bytes().Data() + bytecode_offset_;
    if (key.Length() != key_size_ || MemCompare(base + 4, key.Data(), key_size_) != 0)
    {
        return ByteSpan();
    }
    return ByteSpan(base + 4 + key_size_, bytecode_size_ - 4 - key_size_);
}

}  // namespace engine_placeholder::asset

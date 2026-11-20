#include "ui/drag_drop.h"

#include "log/assert.h"
#include "string/utf8_codec.h"

namespace vse::ui
{

namespace
{
constexpr usize kMaxNameBytes = 127;

// Copies `size` bytes of text, each invalid sequence becoming U+FFFD (EF BF BD).
void SanitizeText(const byte* in, usize size, Vector<byte>& out)
{
    const char* chars = reinterpret_cast<const char*>(in);
    usize offset = 0;
    while (offset < size)
    {
        bool valid = false;
        const DecodedCodepoint dc = DecodeUtf8At(chars, size, offset, &valid);
        const usize length = dc.byte_length == 0 ? 1 : dc.byte_length;
        if (valid)
        {
            for (usize i = 0; i < length; ++i)
            {
                out.Push(in[offset + i]);
            }
        }
        else
        {
            out.Push(byte(0xEF));
            out.Push(byte(0xBF));
            out.Push(byte(0xBD));
        }
        offset += length;
    }
}
}  // namespace

bool DragPayload::Add(DragEntryKind kind, StringView name, const byte* data, usize size)
{
    if (entries_.Size() >= kMaxEntries || size > kMaxEntryBytes)
    {
        return false;
    }
    DragEntry entry;
    entry.kind = kind;
    for (usize i = 0; i < name.Length(); ++i)
    {
        entry.name_chars.Push(name[i]);
    }
    if (kind == DragEntryKind::Text)
    {
        SanitizeText(data, size, entry.bytes);
    }
    else
    {
        for (usize i = 0; i < size; ++i)
        {
            entry.bytes.Push(data[i]);
        }
    }
    entries_.Push(static_cast<DragEntry&&>(entry));
    return true;
}

bool DragPayload::AddText(StringView text)
{
    return Add(DragEntryKind::Text, StringView(), reinterpret_cast<const byte*>(text.Data()), text.Length());
}

bool DragPayload::AddFiles(StringView paths)
{
    return Add(DragEntryKind::FileList, StringView(), reinterpret_cast<const byte*>(paths.Data()), paths.Length());
}

bool DragPayload::AddCustom(StringView name, const byte* data, usize size)
{
    if (name.Length() == 0 || name.Length() > kMaxNameBytes)
    {
        return false;
    }
    return Add(DragEntryKind::Custom, name, data, size);
}

bool DragPayload::AddPointer(StringView tag, void* pointer)
{
    if (tag.Length() == 0 || tag.Length() > kMaxNameBytes || entries_.Size() >= kMaxEntries)
    {
        return false;
    }
    DragEntry entry;
    entry.kind = DragEntryKind::Pointer;
    for (usize i = 0; i < tag.Length(); ++i)
    {
        entry.name_chars.Push(tag[i]);
    }
    entry.pointer = pointer;
    entries_.Push(static_cast<DragEntry&&>(entry));
    return true;
}

bool DragPayload::AddPending(DragEntryKind kind, StringView name)
{
    if (entries_.Size() >= kMaxEntries)
    {
        return false;
    }
    DragEntry entry;
    entry.kind = kind;
    for (usize i = 0; i < name.Length(); ++i)
    {
        entry.name_chars.Push(name[i]);
    }
    entry.pending = true;
    entries_.Push(static_cast<DragEntry&&>(entry));
    return true;
}

const DragEntry& DragPayload::Entry(usize index) const
{
    ENGINE_ASSERT(index < entries_.Size(), "DragPayload::Entry: the index is out of range");
    return entries_[index];
}

const DragEntry* DragPayload::Find(DragEntryKind kind, StringView name) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const DragEntry& e = entries_[i];
        if (e.kind != kind)
        {
            continue;
        }
        if (name.Length() != 0 && e.Name() != name)
        {
            continue;
        }
        return &e;
    }
    return nullptr;
}

StringView DragPayload::Text() const
{
    const DragEntry* e = Find(DragEntryKind::Text);
    return e == nullptr ? StringView() : e->TextView();
}

void* DragPayload::PointerOf(StringView tag) const
{
    const DragEntry* e = Find(DragEntryKind::Pointer, tag);
    return e == nullptr ? nullptr : e->pointer;
}

bool DragPayload::OsRepresentable() const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].kind != DragEntryKind::Pointer && !entries_[i].pending)
        {
            return true;
        }
    }
    return false;
}

bool DragPayload::Pending() const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].pending)
        {
            return true;
        }
    }
    return false;
}

os::CursorShape CursorForDragAction(DragAction action)
{
    switch (action)
    {
    case DragAction::Nothing: return os::CursorShape::NotAllowed;
    case DragAction::Move: return os::CursorShape::ResizeAll;
    case DragAction::Link: return os::CursorShape::Hand;
    case DragAction::Copy: break;
    }
    return os::CursorShape::Arrow;
}

}  // namespace vse::ui

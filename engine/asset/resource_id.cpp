#include "asset/resource_id.h"

#include "os/os.h"
#include "string/normalize.h"
#include "string/utf8.h"

namespace engine_placeholder::asset
{

ResourceId GenerateResourceId()
{
    for (;;)
    {
        u64 value = 0;
        os::FillRandomBytes(&value, sizeof(value));
        if (value != 0)
        {
            return ResourceId::FromValue(value);
        }
    }
}

void FormatResourceId(ResourceId id, char (&out)[17])
{
    static const char digits[] = "0123456789abcdef";
    for (u32 i = 0; i < 16; ++i)
    {
        out[i] = digits[(id.Value() >> (60 - 4 * i)) & 0xf];
    }
    out[16] = '\0';
}

Status ParseResourceId(StringView text, ResourceId* out)
{
    if (text.Length() != 16)
    {
        return Status::Make(ErrorCode::Invalid, "a resource id is exactly 16 hex digits");
    }
    u64 value = 0;
    for (usize i = 0; i < 16; ++i)
    {
        const char c = text[i];
        u64 digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<u64>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<u64>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = static_cast<u64>(c - 'A' + 10);
        }
        else
        {
            return Status::Make(ErrorCode::Invalid, "a resource id is exactly 16 hex digits");
        }
        value = (value << 4) | digit;
    }
    if (value == 0)
    {
        return Status::Make(ErrorCode::Invalid, "the null id cannot be recorded in a source");
    }
    *out = ResourceId::FromValue(value);
    return Status::Success();
}

Status NormalizePath(StringView path, String<HeapAllocator>* out)
{
    if (path.Length() > kMaxPathBytes)
    {
        return Status::Make(ErrorCode::TooLarge, "the path is longer than the path limit");
    }
    if (!IsValidUtf8(path))
    {
        return Status::Make(ErrorCode::Invalid, "the path is not valid UTF-8");
    }
    bool ascii = true;
    for (usize i = 0; i < path.Length() && ascii; ++i)
    {
        ascii = static_cast<u8>(path[i]) < 0x80;
    }
    // ASCII is already in NFC: skip the normalizer (and its scratch arena) for the common case.
    String<HeapAllocator> canonical = ascii ? String<HeapAllocator>(HeapAllocator::Default(), path)
                                            : NormalizeNFC(HeapAllocator::Default(), path);
    const StringView view = canonical.View();
    if (view.Length() > kMaxPathBytes)
    {
        return Status::Make(ErrorCode::TooLarge, "the path is longer than the path limit");
    }
    if (view.Length() < 2 || view[0] != '/')
    {
        return Status::Make(ErrorCode::Invalid, "a path starts with / and names something below the root");
    }
    usize segment_begin = 1;
    for (usize i = 1; i <= view.Length(); ++i)
    {
        const bool end = i == view.Length();
        const char c = end ? '/' : view[i];
        if (!end)
        {
            const u8 byte_value = static_cast<u8>(c);
            if (byte_value < 0x20 || byte_value == 0x7f)
            {
                return Status::Make(ErrorCode::Invalid, "a path has no control characters");
            }
            if (c == '\\')
            {
                return Status::Make(ErrorCode::Invalid, "a path has no backslash; use /");
            }
        }
        if (c != '/')
        {
            continue;
        }
        const usize length = i - segment_begin;
        if (length == 0)
        {
            return Status::Make(ErrorCode::Invalid, "a path has no empty segment and no trailing slash");
        }
        if (length > kMaxPathSegmentBytes)
        {
            return Status::Make(ErrorCode::TooLarge, "a path segment is longer than the segment limit");
        }
        if ((length == 1 && view[segment_begin] == '.') ||
            (length == 2 && view[segment_begin] == '.' && view[segment_begin + 1] == '.'))
        {
            return Status::Make(ErrorCode::Invalid, "a path has no . or .. segment");
        }
        segment_begin = i + 1;
    }
    if (out != nullptr)
    {
        *out = Move(canonical);
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset

#include "platform/clipboard.h"

namespace engine_placeholder::os
{

namespace
{

constexpr byte kReplacement[3] = {0xEF, 0xBF, 0xBD};

// The length of the valid UTF-8 sequence at `p` (1 to 4), or 0 if the bytes there are not one.
usize ValidSequenceLength(const byte* p, usize remaining)
{
    const byte b = p[0];
    if (b < 0x80)
    {
        return 1;
    }
    if (b >= 0xC2 && b <= 0xDF)
    {
        return remaining >= 2 && (p[1] & 0xC0) == 0x80 ? 2 : 0;
    }
    if (b >= 0xE0 && b <= 0xEF)
    {
        if (remaining < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
        {
            return 0;
        }
        if (b == 0xE0 && p[1] < 0xA0)
        {
            return 0;  // overlong
        }
        if (b == 0xED && p[1] >= 0xA0)
        {
            return 0;  // a surrogate
        }
        return 3;
    }
    if (b >= 0xF0 && b <= 0xF4)
    {
        if (remaining < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
        {
            return 0;
        }
        if (b == 0xF0 && p[1] < 0x90)
        {
            return 0;  // overlong
        }
        if (b == 0xF4 && p[1] >= 0x90)
        {
            return 0;  // above U+10FFFF
        }
        return 4;
    }
    return 0;
}

int HexValue(byte c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool IsUnreserved(byte c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~' || c == '/';
}

// Accepts one URI (no CR/LF) as an absolute local path appended to `out`; false if rejected.
bool AcceptUri(const byte* p, usize n, Vector<byte>& out)
{
    constexpr char kPrefix[] = "file://";
    constexpr usize kPrefixLength = sizeof(kPrefix) - 1;
    if (n < kPrefixLength)
    {
        return false;
    }
    for (usize i = 0; i < kPrefixLength; ++i)
    {
        const byte c = p[i];
        const byte lower = (c >= 'A' && c <= 'Z') ? static_cast<byte>(c + 32) : c;
        if (lower != static_cast<byte>(kPrefix[i]))
        {
            return false;
        }
    }
    usize i = kPrefixLength;
    // The host: empty or localhost; the path starts at the next slash.
    usize host_end = i;
    while (host_end < n && p[host_end] != '/')
    {
        ++host_end;
    }
    const usize host_length = host_end - i;
    if (host_length != 0)
    {
        constexpr char kLocalhost[] = "localhost";
        if (host_length != sizeof(kLocalhost) - 1)
        {
            return false;
        }
        for (usize k = 0; k < host_length; ++k)
        {
            const byte c = p[i + k];
            const byte lower = (c >= 'A' && c <= 'Z') ? static_cast<byte>(c + 32) : c;
            if (lower != static_cast<byte>(kLocalhost[k]))
            {
                return false;
            }
        }
    }
    if (host_end >= n)
    {
        return false;  // no path
    }
    Vector<byte> path;
    for (usize k = host_end; k < n; ++k)
    {
        byte c = p[k];
        if (c == '%')
        {
            if (k + 2 >= n)
            {
                return false;
            }
            const int hi = HexValue(p[k + 1]);
            const int lo = HexValue(p[k + 2]);
            if (hi < 0 || lo < 0)
            {
                return false;
            }
            c = static_cast<byte>(hi * 16 + lo);
            k += 2;
        }
        if (c < 0x20 || c == 0x7F)
        {
            return false;  // NUL and control bytes
        }
        path.Push(c);
    }
    if (path.Empty() || path[0] != '/')
    {
        return false;
    }
    // No ".." segment, no "?query" or "#fragment" smuggled in unencoded was already decoded; reject the dot-dot
    // segments.
    usize seg = 0;
    for (usize k = 0; k <= path.Size(); ++k)
    {
        if (k == path.Size() || path[k] == '/')
        {
            if (k - seg == 2 && path[seg] == '.' && path[seg + 1] == '.')
            {
                return false;
            }
            seg = k + 1;
        }
    }
    // The path must be valid UTF-8 to be a UTF-8 path.
    for (usize k = 0; k < path.Size();)
    {
        const usize len = ValidSequenceLength(&path[k], path.Size() - k);
        if (len == 0)
        {
            return false;
        }
        k += len;
    }
    if (!out.Empty())
    {
        out.Push('\n');
    }
    for (usize k = 0; k < path.Size(); ++k)
    {
        out.Push(path[k]);
    }
    return true;
}

}  // namespace

void SanitizeUtf8(const byte* in, usize size, Vector<byte>& out, u32* replaced)
{
    out.Clear();
    u32 count = 0;
    for (usize i = 0; i < size;)
    {
        const usize len = ValidSequenceLength(in + i, size - i);
        if (len == 0)
        {
            for (byte b : kReplacement)
            {
                out.Push(b);
            }
            ++count;
            // Skip the bad lead byte and any continuation bytes that belong to it, so one broken character is one
            // replacement.
            ++i;
            while (i < size && (in[i] & 0xC0) == 0x80)
            {
                ++i;
            }
        }
        else
        {
            for (usize k = 0; k < len; ++k)
            {
                out.Push(in[i + k]);
            }
            i += len;
        }
    }
    if (replaced != nullptr)
    {
        *replaced = count;
    }
}

void Latin1ToUtf8(const byte* in, usize size, Vector<byte>& out)
{
    out.Clear();
    for (usize i = 0; i < size; ++i)
    {
        const byte b = in[i];
        if (b < 0x80)
        {
            out.Push(b);
        }
        else
        {
            out.Push(static_cast<byte>(0xC0 | (b >> 6)));
            out.Push(static_cast<byte>(0x80 | (b & 0x3F)));
        }
    }
}

bool Utf8ToLatin1(const byte* in, usize size, Vector<byte>& out)
{
    out.Clear();
    for (usize i = 0; i < size;)
    {
        const byte b = in[i];
        if (b < 0x80)
        {
            out.Push(b);
            ++i;
        }
        else if ((b == 0xC2 || b == 0xC3) && i + 1 < size && (in[i + 1] & 0xC0) == 0x80)
        {
            out.Push(static_cast<byte>(((b & 0x03) << 6) | (in[i + 1] & 0x3F)));
            i += 2;
        }
        else
        {
            return false;
        }
    }
    return true;
}

void ParseUriList(const byte* in, usize size, Vector<byte>& out, u32* rejected)
{
    out.Clear();
    u32 bad = 0;
    usize line = 0;
    while (line < size)
    {
        usize end = line;
        while (end < size && in[end] != '\n' && in[end] != '\r')
        {
            ++end;
        }
        if (end > line && in[line] != '#')
        {
            if (!AcceptUri(in + line, end - line, out))
            {
                ++bad;
            }
        }
        while (end < size && (in[end] == '\n' || in[end] == '\r'))
        {
            ++end;
        }
        line = end;
    }
    if (rejected != nullptr)
    {
        *rejected = bad;
    }
}

void BuildUriList(const byte* paths, usize size, Vector<byte>& out)
{
    out.Clear();
    constexpr char kHex[] = "0123456789ABCDEF";
    usize start = 0;
    while (start <= size)
    {
        usize end = start;
        while (end < size && paths[end] != '\n')
        {
            ++end;
        }
        if (end > start)
        {
            for (char c : {'f', 'i', 'l', 'e', ':', '/', '/'})
            {
                out.Push(static_cast<byte>(c));
            }
            for (usize i = start; i < end; ++i)
            {
                const byte c = paths[i];
                if (IsUnreserved(c))
                {
                    out.Push(c);
                }
                else
                {
                    out.Push('%');
                    out.Push(static_cast<byte>(kHex[c >> 4]));
                    out.Push(static_cast<byte>(kHex[c & 15]));
                }
            }
            out.Push('\r');
            out.Push('\n');
        }
        start = end + 1;
    }
}

u32 ClipboardRequests::Begin(ClipboardKind kind, ClipboardFormat format, u64 now_ns, InputEventType type)
{
    for (Open& slot : open_)
    {
        if (!slot.used)
        {
            slot.used = true;
            slot.id = next_id_++;
            if (next_id_ == 0)
            {
                next_id_ = 1;
            }
            slot.kind = kind;
            slot.format = format;
            slot.event_type = type;
            slot.deadline_ns = now_ns + kClipboardTimeoutNs;
            ++open_count_;
            return slot.id;
        }
    }
    return 0;
}

bool ClipboardRequests::Pending(u32 id) const
{
    for (const Open& slot : open_)
    {
        if (slot.used && slot.id == id)
        {
            return true;
        }
    }
    return false;
}

void ClipboardRequests::Touch(u32 id, u64 now_ns)
{
    for (Open& slot : open_)
    {
        if (slot.used && slot.id == id)
        {
            slot.deadline_ns = now_ns + kClipboardTimeoutNs;
        }
    }
}

bool ClipboardRequests::Describe(u32 id, ClipboardKind* kind, ClipboardFormat* format) const
{
    for (const Open& slot : open_)
    {
        if (slot.used && slot.id == id)
        {
            if (kind != nullptr)
            {
                *kind = slot.kind;
            }
            if (format != nullptr)
            {
                *format = slot.format;
            }
            return true;
        }
    }
    return false;
}

bool ClipboardRequests::Complete(u32 id, ClipboardStatus status, Vector<byte>&& bytes, u32 replaced, u32 rejected,
                                 u64 now_ns)
{
    Open* open = nullptr;
    for (Open& slot : open_)
    {
        if (slot.used && slot.id == id)
        {
            open = &slot;
        }
    }
    if (open == nullptr)
    {
        return false;
    }
    // Keep the newest results: reuse a free slot, else the oldest one.
    Done* done = nullptr;
    for (Done& d : done_)
    {
        if (!d.used)
        {
            done = &d;
            break;
        }
    }
    if (done == nullptr)
    {
        done = &done_[0];
        for (Done& d : done_)
        {
            if (d.id < done->id)
            {
                done = &d;  // ids grow, so the smallest is the oldest
            }
        }
    }
    done->used = true;
    done->id = id;
    done->result.status = status;
    done->result.kind = open->kind;
    done->result.format = open->format;
    done->result.bytes = static_cast<Vector<byte>&&>(bytes);
    done->result.replaced_sequences = replaced;
    done->result.rejected_entries = rejected;
    if (queue_ != nullptr)
    {
        InputEvent event;
        event.type = open->event_type;
        event.request_id = id;
        event.clipboard_status = static_cast<u8>(status);
        event.clipboard_kind = static_cast<u8>(open->kind);
        event.time_ns = now_ns;
        queue_->Push(event);
    }
    open->used = false;
    --open_count_;
    return true;
}

u32 ClipboardRequests::Expire(u64 now_ns)
{
    u32 count = 0;
    for (Open& slot : open_)
    {
        if (slot.used && now_ns >= slot.deadline_ns)
        {
            Complete(slot.id, ClipboardStatus::Timeout, Vector<byte>(), 0, 0, now_ns);
            ++count;
        }
    }
    return count;
}

void ClipboardRequests::FailAll(ClipboardStatus status, u64 now_ns)
{
    for (Open& slot : open_)
    {
        if (slot.used)
        {
            Complete(slot.id, status, Vector<byte>(), 0, 0, now_ns);
        }
    }
}

bool ClipboardRequests::Take(u32 id, ClipboardResult* out)
{
    for (Done& d : done_)
    {
        if (d.used && d.id == id)
        {
            if (out != nullptr)
            {
                *out = static_cast<ClipboardResult&&>(d.result);
            }
            d.used = false;
            d.result = ClipboardResult();
            return true;
        }
    }
    return false;
}

u32 LimitPathList(Vector<byte>& paths, usize max_entries)
{
    // An entry is a run of bytes between separators; the list holds no empty entries (ParseUriList drops them).
    usize entries = 0;
    usize cut = paths.Size();  // where the first removed byte is (the separator before the first removed entry)
    u32 removed = 0;
    bool in_entry = false;
    for (usize i = 0; i < paths.Size(); ++i)
    {
        if (paths[i] == '\n')
        {
            in_entry = false;
            continue;
        }
        if (!in_entry)
        {
            in_entry = true;
            ++entries;
            if (entries == max_entries + 1)
            {
                cut = (i == 0) ? 0 : i - 1;
            }
            if (entries > max_entries)
            {
                ++removed;
            }
        }
    }
    if (removed == 0)
    {
        return 0;
    }
    paths.Resize(cut);
    return removed;
}

}  // namespace engine_placeholder::os

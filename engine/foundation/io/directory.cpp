// The neutral half of io/directory.h: moving a Directory, listing, wildcards and walking, built on the per-platform
// Directory::Open, Next, Close and DirectoryEntry::Info (io/<platform>/directory.cpp) and detail::GetIdentity
// (io/detail/directory_identity.h).
#include "io/directory.h"
#include "io/detail/directory_identity.h"

namespace vse::io
{

Directory::Directory(Directory&& other) noexcept : handle_(other.handle_), path_(static_cast<Path&&>(other.path_))
{
    other.handle_ = nullptr;
}

Directory& Directory::operator=(Directory&& other) noexcept
{
    if (this != &other)
    {
        Close();
        handle_ = other.handle_;
        path_ = static_cast<Path&&>(other.path_);
        other.handle_ = nullptr;
    }
    return *this;
}

Directory::~Directory()
{
    Close();
}

Status Directory::IsEmpty(const Path& path, bool* empty)
{
    Directory dir;
    if (Status s = Open(path, &dir); s.Failed())
    {
        return s;
    }
    DirectoryEntry entry;
    bool found = false;
    if (Status s = dir.Next(&entry, &found); s.Failed())
    {
        return s;
    }
    *empty = !found;
    return Status::Success();
}

// --- listing
// ---------------------------------------------------------------------------------------------------------------

namespace
{

// Sorts `order` (indices) by the names of `items`, bytewise, with a bottom-up merge sort (no STL).
bool SortIndices(const Vector<DirectoryItem>& items, Vector<usize>* order)
{
    const usize n = order->Size();
    Vector<usize> scratch;
    if (!scratch.TryResize(n))
    {
        return false;
    }
    for (usize width = 1; width < n; width *= 2)
    {
        for (usize lo = 0; lo < n; lo += 2 * width)
        {
            const usize mid = lo + width < n ? lo + width : n;
            const usize hi = lo + 2 * width < n ? lo + 2 * width : n;
            usize a = lo, b = mid, k = lo;
            while (a < mid && b < hi)
            {
                const StringView na = items[(*order)[a]].name.View();
                const StringView nb = items[(*order)[b]].name.View();
                scratch[k++] = (nb.Compare(na) < 0) ? (*order)[b++] : (*order)[a++];
            }
            while (a < mid)
            {
                scratch[k++] = (*order)[a++];
            }
            while (b < hi)
            {
                scratch[k++] = (*order)[b++];
            }
        }
        for (usize i = 0; i < n; ++i)
        {
            (*order)[i] = scratch[i];
        }
    }
    return true;
}

}  // namespace

Status ListDirectory(const Path& path, bool sorted, Vector<DirectoryItem>* out)
{
    Directory dir;
    if (Status s = Directory::Open(path, &dir); s.Failed())
    {
        return s;
    }
    Vector<DirectoryItem> items;
    DirectoryEntry entry;
    bool found = false;
    for (;;)
    {
        if (Status s = dir.Next(&entry, &found); s.Failed())
        {
            return s;
        }
        if (!found)
        {
            break;
        }
        DirectoryItem item;
        if (!Path::Parse(entry.name, path.Style(), &item.name))
        {
            return Status::Of(Error::TooLong);
        }
        item.type = entry.type;
        if (items.Size() == items.Capacity() && !items.TryReserve(items.Capacity() < 16 ? 16 : items.Capacity() * 2))
        {
            return Status::Of(Error::OutOfMemory);
        }
        items.Push(static_cast<DirectoryItem&&>(item));
    }
    if (sorted && items.Size() > 1)
    {
        Vector<usize> order;
        if (!order.TryResize(items.Size()))
        {
            return Status::Of(Error::OutOfMemory);
        }
        for (usize i = 0; i < order.Size(); ++i)
        {
            order[i] = i;
        }
        if (!SortIndices(items, &order))
        {
            return Status::Of(Error::OutOfMemory);
        }
        Vector<DirectoryItem> result;
        if (!result.TryReserve(items.Size()))
        {
            return Status::Of(Error::OutOfMemory);
        }
        for (usize i = 0; i < order.Size(); ++i)
        {
            result.Push(static_cast<DirectoryItem&&>(items[order[i]]));
        }
        items = static_cast<Vector<DirectoryItem>&&>(result);
    }
    *out = static_cast<Vector<DirectoryItem>&&>(items);
    return Status::Success();
}

// --- wildcard
// --------------------------------------------------------------------------------------------------------------

namespace
{
// Length of the UTF-8 sequence that starts with `lead` (1 for a malformed lead, so matching always advances).
usize SequenceLength(byte lead)
{
    if (lead < 0x80)
    {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0)
    {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0)
    {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0)
    {
        return 4;
    }
    return 1;
}
}  // namespace

bool MatchesWildcard(StringView pattern, StringView name)
{
    usize p = 0, n = 0;
    usize star_p = pattern.Length();  // pattern position after the last `*`, or Length() when none
    usize star_n = 0;
    bool have_star = false;
    while (n < name.Length())
    {
        bool matched = false;
        if (p < pattern.Length())
        {
            const char pc = pattern[p];
            if (pc == '*')
            {
                have_star = true;
                star_p = ++p;
                star_n = n;
                continue;
            }
            if (pc == '?')
            {
                const usize len = SequenceLength(static_cast<byte>(name[n]));
                n += len < name.Length() - n ? len : name.Length() - n;
                ++p;
                continue;
            }
            usize lit = p;
            if (pc == '\\' && p + 1 < pattern.Length())
            {
                lit = p + 1;
            }
            if (pattern[lit] == name[n])
            {
                p = lit + 1;
                ++n;
                matched = true;
            }
        }
        if (matched)
        {
            continue;
        }
        if (!have_star)
        {
            return false;
        }
        // Backtrack: let the last `*` swallow one more character.
        const usize len = SequenceLength(static_cast<byte>(name[star_n]));
        star_n += len < name.Length() - star_n ? len : name.Length() - star_n;
        n = star_n;
        p = star_p;
    }
    while (p < pattern.Length() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.Length();
}

// --- walking
// ---------------------------------------------------------------------------------------------------------------

namespace
{

constexpr u32 kMaxWalkDepth = 128;

using detail::Identity;

// What a directory being walked reads its entries from: an open stream, or a sorted listing.
struct Source
{
    Directory dir;
    Vector<DirectoryItem> sorted;
    usize index = 0;
};

struct WalkState
{
    const WalkOptions* options;
    WalkVisitor visitor;
    void* user;
    Vector<Identity> ancestors;  // directories on the current path, for loop detection
    bool stopped = false;
};

Status OpenSource(const WalkOptions& options, const Path& path, Source* source)
{
    return options.sorted ? ListDirectory(path, true, &source->sorted) : Directory::Open(path, &source->dir);
}

WalkAction Report(WalkState& st, const Path& path, StringView name, FileType type, u32 depth, bool after,
                  const Status& error)
{
    WalkEntry we{path, name, type, depth, after, error};
    const WalkAction action = st.visitor(st.user, we);
    if (action == WalkAction::Stop)
    {
        st.stopped = true;
    }
    return action;
}

void WalkDir(WalkState& st, Source& source, const Path& dir_path, u32 depth)
{
    for (;;)
    {
        // Next entry, from the sorted list or the stream.
        StringView name;
        FileType type = FileType::None;
        DirectoryEntry entry;
        if (st.options->sorted)
        {
            if (source.index >= source.sorted.Size())
            {
                return;
            }
            name = source.sorted[source.index].name.View();
            type = source.sorted[source.index].type;
            ++source.index;
        }
        else
        {
            bool found = false;
            const Status s = source.dir.Next(&entry, &found);
            if (s.Failed())
            {
                Report(st, dir_path, dir_path.FileName(), FileType::Directory, depth, false, s);
                return;
            }
            if (!found)
            {
                return;
            }
            name = entry.name;
            type = entry.type;
        }
        Path path;
        if (!dir_path.TryJoin(Path(name, dir_path.Style()), &path))
        {
            if (Report(st, dir_path, name, type, depth + 1, false, Status::Of(Error::TooLong)) == WalkAction::Stop)
            {
                return;
            }
            continue;
        }
        const bool matches = st.options->name_pattern.Empty() || MatchesWildcard(st.options->name_pattern, name);

        // May this entry be entered? Settle that, and open it, before reporting, so it is reported once.
        bool descend = false;
        Status error;
        Source child;
        Identity id{};
        if (depth + 1 < st.options->max_depth)
        {
            bool is_dir = type == FileType::Directory;
            if (type == FileType::Symlink && st.options->follow_symlinks)
            {
                FileInfo target;
                is_dir = Stat(path, &target).Ok() && target.type == FileType::Directory;
            }
            if (is_dir)
            {
                error = detail::GetIdentity(path, &id);
                if (error.Ok())
                {
                    for (usize i = 0; i < st.ancestors.Size(); ++i)
                    {
                        if (st.ancestors[i].device == id.device && st.ancestors[i].inode == id.inode)
                        {
                            error = Status::Of(Error::SymlinkLoop);
                        }
                    }
                }
                if (error.Ok())
                {
                    error = OpenSource(*st.options, path, &child);
                }
                if (error.Ok() && !st.ancestors.TryReserve(st.ancestors.Size() + 1))
                {
                    error = Status::Of(Error::OutOfMemory);
                }
                descend = error.Ok();
            }
        }

        WalkAction action = WalkAction::Continue;
        if (!st.options->post_order && (matches || error.Failed()))
        {
            action = Report(st, path, name, type, depth + 1, false, error);
            if (st.stopped)
            {
                return;
            }
        }
        bool descended = false;
        if (descend && action != WalkAction::SkipSubtree)
        {
            descended = true;
            st.ancestors.Push(id);
            WalkDir(st, child, path, depth + 1);
            st.ancestors.Pop();
            if (st.stopped)
            {
                return;
            }
        }
        if (st.options->post_order && (matches || error.Failed()))
        {
            // A directory that was entered is reported after its children; everything else is reported once, in place.
            Report(st, path, name, type, depth + 1, descended, error);
            if (st.stopped)
            {
                return;
            }
        }
    }
}

}  // namespace

Status WalkDirectory(const Path& root, const WalkOptions& options, WalkVisitor visitor, void* user)
{
    if (visitor == nullptr || options.max_depth == 0 || options.max_depth > kMaxWalkDepth)
    {
        return Status::Of(Error::Invalid);
    }
    Source source;
    if (Status s = OpenSource(options, root, &source); s.Failed())
    {
        return s;  // the caller gets the real error for the root
    }
    WalkState st;
    st.options = &options;
    st.visitor = visitor;
    st.user = user;
    Identity id;
    if (detail::GetIdentity(root, &id).Ok())
    {
        if (!st.ancestors.TryReserve(1))
        {
            return Status::Of(Error::OutOfMemory);  // the loop check needs the root
        }
        st.ancestors.Push(id);
    }
    WalkDir(st, source, root, 0);
    return Status::Success();
}

}  // namespace vse::io

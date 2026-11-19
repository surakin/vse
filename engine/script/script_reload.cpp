#include "script/script_reload.h"

#include "asset/content_hash.h"
#include "containers/shared_bytes.h"
#include "io/file.h"

namespace engine_placeholder::script
{
namespace
{

u64 TextHash(ByteSpan bytes)
{
    asset::ContentHasher hasher;
    hasher.Update(bytes);
    const asset::ContentHash hash = hasher.Finish();
    u64 value = 0;
    for (usize i = 0; i < 8; ++i)
    {
        value |= static_cast<u64>(hash.bytes[i]) << (8 * i);
    }
    return value;
}

// The line a backend message names (`name:LINE: text`), or 0.
u32 LineOf(StringView message)
{
    for (usize i = 0; i + 1 < message.Length(); ++i)
    {
        if (message[i] == ':' && message[i + 1] >= '0' && message[i + 1] <= '9')
        {
            u32 value = 0;
            usize j = i + 1;
            while (j < message.Length() && message[j] >= '0' && message[j] <= '9' && value < 100000000)
            {
                value = value * 10 + static_cast<u32>(message[j] - '0');
                ++j;
            }
            return j < message.Length() && message[j] == ':' ? value : 0;
        }
    }
    return 0;
}

bool ReadFileAll(const io::Path& path, Vector<u8>* out)
{
    io::FilePtr file;
    return !io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file).Failed() &&
           !io::ReadAll(*file, out, u64{1} << 20).Failed();
}

}  // namespace

ScriptReloader::ScriptReloader(ScriptWorld* world, Backend* backend, bool enabled)
    : world_(world), backend_(backend), enabled_(enabled), last_tick_(HeapAllocator::Default())
{
}

Outcome ScriptReloader::Reload(Span<const ModuleReplacement> changes, u64 tick, ReloadReport* report)
{
    *report = ReloadReport();
    report->enabled = enabled_;
    if (!enabled_ || changes.Size() == 0)
    {
        return Outcome();
    }
    // The last text of each key; a key reloaded in this tick waits.
    Vector<ModuleReplacement> accepted;
    for (usize i = 0; i < changes.Size(); ++i)
    {
        bool later = false;
        for (usize j = i + 1; j < changes.Size(); ++j)
        {
            later = later || changes[j].key.View() == changes[i].key.View();
        }
        if (later)
        {
            continue;
        }
        ++report->requested;
        const u64* last = last_tick_.Find(changes[i].key.View());
        if (last != nullptr && *last == tick)
        {
            report->deferred.Push(String<HeapAllocator>(HeapAllocator::Default(), changes[i].key.View()));
            continue;
        }
        ModuleReplacement copy;
        copy.key = String<HeapAllocator>(HeapAllocator::Default(), changes[i].key.View());
        copy.bytes = changes[i].bytes;
        copy.offset = changes[i].offset;
        copy.size = changes[i].size;
        accepted.Push(Move(copy));
    }
    // The compile check, in a scratch state: a broken text never reaches a running partition.
    Vector<ModuleReplacement> good;
    UniquePtr<State> scratch = backend_->CreateState(u64{64} << 20);
    for (usize i = 0; i < accepted.Size(); ++i)
    {
        const ByteSpan bytes = accepted[i].bytes != nullptr ? accepted[i].bytes->Bytes() : ByteSpan();
        Outcome compiled(Status::Invalid, "the text lies outside its buffer");
        if (scratch != nullptr && accepted[i].offset <= bytes.Size() &&
            accepted[i].size <= bytes.Size() - accepted[i].offset)
        {
            compiled = scratch->Compile(
                accepted[i].key.View(),
                StringView(reinterpret_cast<const char*>(bytes.Data()) + accepted[i].offset, accepted[i].size),
                nullptr);
        }
        if (!compiled.IsOk())
        {
            ReloadProblem problem;
            problem.key = String<HeapAllocator>(HeapAllocator::Default(), accepted[i].key.View());
            problem.line = LineOf(compiled.message.View());
            problem.message = String<HeapAllocator>(HeapAllocator::Default(), compiled.message.View());
            report->compile_failures.Push(Move(problem));
            continue;
        }
        good.Push(Move(accepted[i]));
    }
    if (good.Size() == 0)
    {
        return Outcome();
    }
    const Outcome applied = world_->ReloadModules(Span<const ModuleReplacement>(good.Data(), good.Size()),
                                                  &report->result);
    if (!applied.IsOk())
    {
        report->run_failure.message = String<HeapAllocator>(HeapAllocator::Default(), applied.message.View());
        report->run_failure.line = LineOf(applied.message.View());
        return Outcome(applied.status, applied.message.View());
    }
    for (usize i = 0; i < good.Size(); ++i)
    {
        last_tick_.Insert(good[i].key.View(), tick);
    }
    report->applied = true;
    return Outcome();
}

ScriptDevWatcher::ScriptDevWatcher(StringView directory)
{
    active_ = directory.Length() != 0 && io::Path::Parse(directory, io::NativePathStyle(), &directory_);
    if (active_)
    {
        io::WatchOptions options;
        options.poll_interval_ms = 100;
        if (io::DirectoryWatcher::Open(directory_, options, &events_).Failed())
        {
            events_ = UniquePtr<io::DirectoryWatcher>();  // no watcher: every poll reads every file
        }
    }
}

void ScriptDevWatcher::Watch(asset::ResourceId id)
{
    if (!active_)
    {
        return;
    }
    for (usize i = 0; i < watched_.Size(); ++i)
    {
        if (watched_[i].id == id)
        {
            return;
        }
    }
    Watched w;
    w.id = id;
    char hex[17];
    asset::FormatResourceId(id, hex);
    String<HeapAllocator> name(HeapAllocator::Default(), StringView(hex, 16));
    name.Append(StringView(".lua"));
    io::Path relative;
    io::Path path;
    Vector<u8> text;
    if (io::Path::Parse(name.View(), io::NativePathStyle(), &relative) && directory_.TryJoin(relative, &path) &&
        ReadFileAll(path, &text))
    {
        w.hash = TextHash(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Size()));
        w.known = true;
    }
    watched_.Push(w);
}

void ScriptDevWatcher::Poll(Vector<DevChange>* out)
{
    out->Clear();
    if (!active_)
    {
        return;
    }
    if (events_ == nullptr)
    {
        rescan_ = true;
    }
    else
    {
        Vector<io::WatchEvent> events;
        if (events_->Poll(&events).Failed())
        {
            rescan_ = true;  // the directory went away or the watch broke: read everything until it is back
        }
        for (usize e = 0; e < events.Size(); ++e)
        {
            if (events[e].kind == io::WatchEventKind::Overflow)
            {
                rescan_ = true;
                continue;
            }
            for (usize i = 0; i < watched_.Size(); ++i)
            {
                char want[17];
                asset::FormatResourceId(watched_[i].id, want);
                const StringView n = events[e].name.View();
                if (n.Length() == 20 && n.Substring(0, 16) == StringView(want, 16) &&
                    n.Substring(16, 4) == StringView(".lua"))
                {
                    watched_[i].dirty = true;
                }
            }
        }
    }
    const bool all = rescan_;
    rescan_ = false;
    for (usize i = 0; i < watched_.Size(); ++i)
    {
        if (!all && !watched_[i].dirty)
        {
            continue;
        }
        watched_[i].dirty = false;
        char hex[17];
        asset::FormatResourceId(watched_[i].id, hex);
        String<HeapAllocator> name(HeapAllocator::Default(), StringView(hex, 16));
        name.Append(StringView(".lua"));
        io::Path relative;
        io::Path path;
        Vector<u8> text;
        if (!io::Path::Parse(name.View(), io::NativePathStyle(), &relative) || !directory_.TryJoin(relative, &path) ||
            !ReadFileAll(path, &text) || text.Size() == 0)
        {
            watched_[i].dirty = true;  // unreadable or empty now (a write in progress): look again next poll
            continue;
        }
        const ByteSpan bytes(reinterpret_cast<const byte*>(text.Data()), text.Size());
        const u64 hash = TextHash(bytes);
        if (watched_[i].known && watched_[i].hash == hash)
        {
            continue;
        }
        DevChange change;
        change.id = watched_[i].id;
        change.text = CopyShared(bytes);
        change.hash = hash;
        watched_[i].dirty = true;  // reported again by every poll until `Applied` says it was handled
        out->Push(Move(change));
    }
}

void ScriptDevWatcher::Applied(const DevChange& change)
{
    for (usize i = 0; i < watched_.Size(); ++i)
    {
        if (watched_[i].id == change.id)
        {
            watched_[i].hash = change.hash;
            watched_[i].known = true;
            watched_[i].dirty = false;
        }
    }
}

}  // namespace engine_placeholder::script

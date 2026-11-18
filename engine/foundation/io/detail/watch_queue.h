#pragma once

// The queue every directory watcher backend fills (Phase 27 Day 464): it merges the events of one name and bounds the
// backlog. Internal to the file layer.
#include "io/directory_watcher.h"

namespace engine_placeholder::io::detail
{

/// The most events kept before the queue collapses into one `Overflow`.
constexpr usize kMaxQueuedWatchEvents = 4096;

/// A list of events merged per name. Not thread-safe: the backend locks around it.
class WatchQueue
{
public:
    /// Adds one event, merging it with the last event of the same name: a `Created` then `Modified` stays `Created`, a
    /// `Created` then `Removed` is nothing, a `Removed` then `Created` is a `Modified`, a `Modified` then `Removed` is
    /// a `Removed`, and repeats collapse. A `Renamed` is never merged and stops the merge across it.
    void Push(WatchEventKind kind, StringView name, StringView old_name = StringView())
    {
        if (overflowed_)
        {
            return;
        }
        if (kind == WatchEventKind::Overflow)
        {
            Collapse();
            return;
        }
        if (kind != WatchEventKind::Renamed)
        {
            for (usize i = events_.Size(); i > 0; --i)
            {
                WatchEvent& e = events_[i - 1];
                const bool names_it = e.name.View() == name ||
                                      (e.kind == WatchEventKind::Renamed && e.old_name.View() == name);
                if (!names_it)
                {
                    continue;
                }
                if (e.kind == WatchEventKind::Renamed || e.kind == WatchEventKind::Overflow)
                {
                    break;
                }
                if (Merge(i - 1, kind))
                {
                    return;
                }
                break;
            }
        }
        if (events_.Size() >= kMaxQueuedWatchEvents)
        {
            Collapse();
            return;
        }
        WatchEvent event;
        event.kind = kind;
        event.name = String<HeapAllocator>(HeapAllocator::Default(), name);
        event.old_name = String<HeapAllocator>(HeapAllocator::Default(), old_name);
        events_.Push(Move(event));
    }

    /// Whether an event is waiting.
    bool Empty() const { return events_.Size() == 0; }

    /// Moves every event to the end of `out` and empties the queue.
    void Take(Vector<WatchEvent>* out)
    {
        for (usize i = 0; i < events_.Size(); ++i)
        {
            out->Push(Move(events_[i]));
        }
        events_.Clear();
        overflowed_ = false;
    }

private:
    void Collapse()
    {
        events_.Clear();
        events_.Push(WatchEvent());  // kind Overflow, no names
        overflowed_ = true;
    }

    // Merges `kind` into the event at `index` of the same name; false when the two stay separate.
    bool Merge(usize index, WatchEventKind kind)
    {
        WatchEvent& e = events_[index];
        if (kind == WatchEventKind::Modified)
        {
            return e.kind == WatchEventKind::Created || e.kind == WatchEventKind::Modified;
        }
        if (kind == WatchEventKind::Created)
        {
            if (e.kind == WatchEventKind::Removed)
            {
                e.kind = WatchEventKind::Modified;  // replaced: same name, new contents
                return true;
            }
            return e.kind == WatchEventKind::Created;
        }
        // Removed
        if (e.kind == WatchEventKind::Created)
        {
            Vector<WatchEvent> keep;
            for (usize i = 0; i < events_.Size(); ++i)
            {
                if (i != index)
                {
                    keep.Push(Move(events_[i]));
                }
            }
            events_ = Move(keep);
            return true;
        }
        if (e.kind == WatchEventKind::Modified)
        {
            e.kind = WatchEventKind::Removed;
            return true;
        }
        return e.kind == WatchEventKind::Removed;
    }

    Vector<WatchEvent> events_;
    bool overflowed_ = false;
};

}  // namespace engine_placeholder::io::detail

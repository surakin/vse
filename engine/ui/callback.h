#pragma once

#include "containers/array.h"
#include "containers/vector.h"
#include "types.h"

//! A list of listeners without `std::function`: a function pointer and a user pointer each,
//! disconnected by token.

namespace vse::ui
{

/// The token `Connect` returns; 0 is never a valid token.
using ConnectionId = u32;

/// A set of listeners that are called with `Args...`.
///
/// A listener is a plain function `void (*)(void* user, Args...)` and the `user` pointer given when it was connected.
/// Listeners are called in the order they were connected. A listener may connect or disconnect (itself or another)
/// while the callback runs: the call goes on over a snapshot, and a listener disconnected during the call is not
/// called.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the list; the user pointers are borrowed and must outlive the connection.
template <typename... Args> class Callback
{
public:
    /// The listener signature.
    using Function = void (*)(void* user, Args... args);

    /// Adds a listener.
    /// - Param function: the function to call; must not be null.
    /// - Param user: passed to it as the first argument.
    /// - Returns: the token for `Disconnect`.
    /// - Ownership: borrows `user`, which must outlive the connection.
    ConnectionId Connect(Function function, void* user)
    {
        Entry e;
        e.function = function;
        e.user = user;
        e.id = ++next_id_;
        entries_.Push(e);
        return e.id;
    }

    /// Removes the listener `id`.
    /// - Returns: whether it was connected.
    /// - Fails: returns false for an unknown or already disconnected token.
    bool Disconnect(ConnectionId id)
    {
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            if (entries_[i].id == id)
            {
                entries_.Remove(i);
                return true;
            }
        }
        return false;
    }

    /// Calls every listener with `args`.
    void Invoke(Args... args)
    {
        // A listener may connect or disconnect listeners while it runs, so the call walks a copy taken first. The
        // usual few listeners are copied into the stack (container audit Day 5: the copy was a heap Vector on every
        // call, even with no listener).
        const usize n = entries_.Size();
        if (n == 0)
        {
            return;
        }
        if (n <= kInline)
        {
            Array<Entry, kInline> copy;
            for (usize i = 0; i < n; ++i)
            {
                copy[i] = entries_[i];
            }
            CallEach(copy.Data(), n, args...);
            return;
        }
        Vector<Entry> snapshot;
        snapshot.Append(entries_.Data(), n);
        CallEach(snapshot.Data(), n, args...);
    }

    /// Returns how many listeners are connected.
    usize Count() const { return entries_.Size(); }

private:
    static constexpr usize kInline = 4;

    struct Entry
    {
        Function function = nullptr;
        void* user = nullptr;
        ConnectionId id = 0;
    };

    void CallEach(const Entry* entries, usize n, Args... args)
    {
        for (usize i = 0; i < n; ++i)
        {
            if (Connected(entries[i].id))
            {
                entries[i].function(entries[i].user, args...);
            }
        }
    }
    bool Connected(ConnectionId id) const
    {
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            if (entries_[i].id == id)
            {
                return true;
            }
        }
        return false;
    }
    Vector<Entry> entries_;
    ConnectionId next_id_ = 0;
};

}  // namespace vse::ui

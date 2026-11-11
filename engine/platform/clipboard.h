#pragma once

#include "containers/vector.h"
#include "platform/input_event.h"
#include "string/string.h"
#include "types.h"

//! The OS clipboard: what can be put on it, the answer to a request, and the rules that keep data from other programs
//! from doing harm.
//!
//! Reading is asynchronous: [[os::Window::RequestClipboard]] returns an id, a later `PumpEvents` queues an
//! [[os::InputEventType::ClipboardData]] event with that id, and
//! [[os::Window::TakeClipboardResult]] hands over the bytes. A request nobody answers ends with
//! [[os::ClipboardStatus::Timeout]] after two seconds, so a paste never hangs a program.

namespace engine_placeholder::os
{

/// Which selection: the one Ctrl+C and Ctrl+V use, or the X11/Wayland "primary" one that selecting text fills and a
/// middle click pastes.
enum class ClipboardKind : u8
{
    Clipboard = 0,  ///< The ordinary clipboard.
    Primary,        ///< The primary selection (Windows and macOS have none and report it unsupported).
    Count,          ///< The number of kinds; not a kind.
};

/// What the data is.
enum class ClipboardFormat : u8
{
    Text = 0,  ///< UTF-8 text.
    FileList,  ///< Absolute local paths, UTF-8, one per line (separated by `\n`).
    Custom,    ///< Bytes under a name of the program's choosing (a MIME type such as `application/x-myengine-entity`).
};

/// How a request ended.
enum class ClipboardStatus : u8
{
    Ok = 0,    ///< The bytes are in the result.
    Timeout,   ///< Nobody answered in time (the owner is gone or stuck).
    Missing,   ///< The selection is empty or its owner does not offer the format.
    TooLarge,  ///< The data is above [[os::kMaxClipboardBytes]].
    Failed,    ///< The transfer broke (the owner closed the connection early, a protocol error).
};

/// The most bytes a clipboard transfer may carry, either way; more is refused (`TooLarge`). 16 MiB.
inline constexpr usize kMaxClipboardBytes = usize{16} * 1024 * 1024;

/// How long a request waits for any progress, in nanoseconds (2 s).
inline constexpr u64 kClipboardTimeoutNs = 2'000'000'000ull;

/// What a program puts on the clipboard: a format and bytes. Borrowed for the call; the window copies it.
///
/// `Text` bytes are UTF-8 (invalid sequences are replaced before they are offered). `FileList` bytes are absolute paths
/// separated by `\\n`. `Custom` needs `custom_name`.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows `data` and `custom_name`.
struct ClipboardOffer
{
    ClipboardFormat format = ClipboardFormat::Text;  ///< What the bytes are.
    StringView custom_name;                          ///< `Custom`: the MIME type or atom name.
    const byte* data = nullptr;                      ///< The bytes.
    usize size = 0;                                  ///< Their count.
};

/// The finished answer to a request.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns `bytes`.
struct ClipboardResult
{
    ClipboardStatus status = ClipboardStatus::Failed;  ///< How the request ended.
    ClipboardKind kind = ClipboardKind::Clipboard;     ///< Which selection was asked.
    ClipboardFormat format = ClipboardFormat::Text;    ///< What was asked for.
    Vector<byte> bytes;  ///< The data: valid UTF-8 for `Text`, absolute paths separated by `\n` for `FileList`, raw
                         ///< bytes for `Custom`. Empty unless `Ok`.
    u32 replaced_sequences = 0;  ///< `Text`: invalid UTF-8 sequences that were replaced by U+FFFD.
    u32 rejected_entries = 0;    ///< `FileList`: entries dropped because they were not absolute local `file://` paths.
};

/// Makes the UTF-8 text valid: each invalid sequence (a stray continuation byte, an overlong form, a surrogate, a value
/// above U+10FFFF, a truncated sequence) becomes U+FFFD.
/// - Param in: the bytes.
/// - Param size: their count.
/// - Param out: receives the valid text (cleared first).
/// - Param replaced: if non-null, receives how many sequences were replaced.
void SanitizeUtf8(const byte* in, usize size, Vector<byte>& out, u32* replaced);

/// Converts Latin-1 bytes to UTF-8 (the X11 `STRING` target).
void Latin1ToUtf8(const byte* in, usize size, Vector<byte>& out);

/// Converts UTF-8 to Latin-1 if every scalar is U+00FF or below.
/// - Returns: false, leaving `out` unspecified, if some scalar does not fit (then the `STRING` target is refused rather
/// than made lossy).
bool Utf8ToLatin1(const byte* in, usize size, Vector<byte>& out);

/// Reads a `text/uri-list` into absolute local paths.
///
/// Lines starting with `#` are comments. An entry is accepted only if it is `file://` with an empty host or
/// `localhost`, a percent-decoded path that is absolute, has no NUL or control byte and no `..` segment; everything
/// else (`http:`, relative paths, other hosts) is rejected and counted. No network URI is ever followed.
/// - Param in: the list.
/// - Param size: its byte count.
/// - Param out: receives the accepted paths separated by `\n` (cleared first).
/// - Param rejected: if non-null, receives the count of rejected entries.
void ParseUriList(const byte* in, usize size, Vector<byte>& out, u32* rejected);

/// Builds a `text/uri-list` (`file://` URIs, percent-encoded, CRLF-terminated) from paths separated by `\n`.
void BuildUriList(const byte* paths, usize size, Vector<byte>& out);

/// The bookkeeping of asynchronous requests, shared by the backends: ids, the two-second timeout, the finished results
/// and the events that announce them.
///
/// A request completes once (the first of its answer and its timeout). At most [[os::ClipboardRequests::kMaxInFlight]]
/// requests are open and
/// [[os::ClipboardRequests::kMaxUnread]] finished results are kept; when more finish unread the oldest is dropped. Time
/// is passed in by the caller, so a test drives it with a fake clock.
///
/// - Thread-safety: not-thread-safe; owner-thread only.
/// - Ownership: owns pending requests and finished results; borrows the queue, which must outlive it.
class ClipboardRequests
{
public:
    /// The most requests open at once.
    static constexpr u32 kMaxInFlight = 8;
    /// The most finished results kept for [[os::ClipboardRequests::Take]].
    static constexpr u32 kMaxUnread = 8;

    /// Attaches the queue that receives the `ClipboardData` events.
    void Init(InputEventQueue* queue) { queue_ = queue; }

    /// Opens a request.
    /// - Returns: its id (never 0), or 0 if [[os::ClipboardRequests::kMaxInFlight]] are already open.
    /// - Param type: the event queued when the request finishes (`ClipboardData`, or `DropData` for a drop).
    u32 Begin(ClipboardKind kind, ClipboardFormat format, u64 now_ns,
              InputEventType type = InputEventType::ClipboardData);

    /// Returns whether the request is open.
    bool Pending(u32 id) const;

    /// Restarts the request's timeout (a transfer that makes progress does not time out).
    void Touch(u32 id, u64 now_ns);

    /// Finishes a request with its answer and queues the event. Returns false if the id is not open (already finished
    /// or timed out): then nothing happens.
    bool Complete(u32 id, ClipboardStatus status, Vector<byte>&& bytes, u32 replaced, u32 rejected, u64 now_ns);

    /// Finishes a request without data.
    bool Fail(u32 id, ClipboardStatus status, u64 now_ns) { return Complete(id, status, Vector<byte>(), 0, 0, now_ns); }

    /// Finishes every open request whose timeout has passed with `Timeout`.
    /// - Returns: how many.
    u32 Expire(u64 now_ns);

    /// Finishes every open request with `status` (the window is closing, or the transfer machinery was reset).
    void FailAll(ClipboardStatus status, u64 now_ns);

    /// Hands over a finished result once.
    /// - Returns: false if the id has no unread result.
    bool Take(u32 id, ClipboardResult* out);

    /// Returns the kind and format of an open request (for the backend to choose a target).
    /// - Returns: false if the id is not open.
    bool Describe(u32 id, ClipboardKind* kind, ClipboardFormat* format) const;

    /// Returns the number of open requests.
    u32 InFlight() const { return open_count_; }

private:
    struct Open
    {
        u32 id = 0;
        ClipboardKind kind = ClipboardKind::Clipboard;
        ClipboardFormat format = ClipboardFormat::Text;
        InputEventType event_type = InputEventType::ClipboardData;
        u64 deadline_ns = 0;
        bool used = false;
    };
    struct Done
    {
        u32 id = 0;
        ClipboardResult result;
        bool used = false;
    };

    InputEventQueue* queue_ = nullptr;
    Open open_[kMaxInFlight];
    Done done_[kMaxUnread];
    u32 open_count_ = 0;
    u32 next_id_ = 1;
};

}  // namespace engine_placeholder::os

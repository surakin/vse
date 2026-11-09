#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include <unistd.h>

#include <fcntl.h>

#include "containers/vector.h"
#include "os/os.h"
#include "platform/clipboard.h"
#include "platform/input_event.h"
#include "platform/linux/clipboard_pipe.h"
#include "platform/linux/wayland/wayland_clipboard.h"
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/linux/x11/x11_window.h"
#include "platform/window.h"
#include "lang/initializer_list.h"

// Day 187: the clipboard value rules (UTF-8, Latin-1, file lists, request bookkeeping), the non-blocking pipes, and the X11 selection protocol between two connections.
namespace window_clipboard_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;

namespace {

Vector<byte> Bytes(const char* text) {
    Vector<byte> out;
    for (usize i = 0; text[i] != '\0'; ++i) out.Push(static_cast<byte>(text[i]));
    return out;
}

Vector<byte> Raw(InitializerList<unsigned> values) {
    Vector<byte> out;
    for (unsigned v : values) out.Push(static_cast<byte>(v));
    return out;
}

bool Equal(const Vector<byte>& a, const Vector<byte>& b) {
    if (a.Size() != b.Size()) return false;
    for (usize i = 0; i < a.Size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

bool Equal(const Vector<byte>& a, const char* text) { return Equal(a, Bytes(text)); }

Vector<byte> Sanitized(const Vector<byte>& in, u32* replaced = nullptr) {
    Vector<byte> out;
    SanitizeUtf8(in.Data(), in.Size(), out, replaced);
    return out;
}

Vector<byte> Parsed(const char* list, u32* rejected = nullptr) {
    const Vector<byte> in = Bytes(list);
    Vector<byte> out;
    ParseUriList(in.Data(), in.Size(), out, rejected);
    return out;
}

constexpr u64 kSecond = 1'000'000'000ull;

} // namespace

TEST_CASE("Clipboard: valid UTF-8 (ASCII, accents, a combining mark, a 4-byte emoji, nothing) passes unchanged", "[clipboard][os]") {
    const char* samples[] = {"plain ascii", "caf\xC3\xA9 na\xC3\xAFve", "e\xCC\x81 (e + combining acute)", "party \xF0\x9F\x8E\x89!", ""};
    for (const char* sample : samples) {
        u32 replaced = 99;
        const Vector<byte> out = Sanitized(Bytes(sample), &replaced);
        CHECK(Equal(out, sample));
        CHECK(replaced == 0);
    }
}

TEST_CASE("Clipboard: invalid UTF-8 becomes U+FFFD and is counted", "[clipboard][os]") {
    u32 replaced = 0;
    // a stray continuation byte, an overlong '/', a surrogate, above U+10FFFF, and a truncated 3-byte sequence at the end
    Vector<byte> out = Sanitized(Raw({'a', 0x80, 'b', 0xC0, 0xAF, 'c', 0xED, 0xA0, 0x80, 'd', 0xF4, 0x90, 0x80, 0x80, 'e', 0xE2, 0x82}), &replaced);
    CHECK(replaced >= 5);
    // Whatever came out is valid: sanitizing it again changes nothing.
    u32 again = 0;
    const Vector<byte> twice = Sanitized(out, &again);
    CHECK(again == 0);
    CHECK(Equal(out, twice));
    // The ASCII letters survived in order.
    Vector<byte> letters;
    for (usize i = 0; i < out.Size(); ++i) {
        if (out[i] >= 'a' && out[i] <= 'e') letters.Push(out[i]);
    }
    CHECK(Equal(letters, "abcde"));
}

TEST_CASE("Clipboard: Latin-1 converts to UTF-8 and back when lossless, and is refused when a scalar does not fit", "[clipboard][os]") {
    const Vector<byte> latin = Raw({'c', 'a', 'f', 0xE9, ' ', 0xFF});
    Vector<byte> utf8;
    Latin1ToUtf8(latin.Data(), latin.Size(), utf8);
    CHECK(Equal(utf8, "caf\xC3\xA9 \xC3\xBF"));
    Vector<byte> back;
    CHECK(Utf8ToLatin1(utf8.Data(), utf8.Size(), back));
    CHECK(Equal(back, latin));

    const Vector<byte> wide = Bytes("snow \xE2\x98\x83");
    Vector<byte> refused;
    CHECK_FALSE(Utf8ToLatin1(wide.Data(), wide.Size(), refused));
}

TEST_CASE("Clipboard: a file list keeps absolute local paths, percent-decoded, and rejects everything else", "[clipboard][os]") {
    u32 rejected = 0;
    const Vector<byte> out = Parsed("# a comment\r\nfile:///home/user/a%20b.txt\r\nfile://localhost/tmp/c.png\r\n", &rejected);
    CHECK(Equal(out, "/home/user/a b.txt\n/tmp/c.png"));
    CHECK(rejected == 0);
}

TEST_CASE("Clipboard: a hostile file list is rejected entry by entry", "[clipboard][os]") {
    u32 rejected = 0;
    const Vector<byte> out = Parsed(
        "http://example.com/x\r\n"            // a network URL
        "file://otherhost/share/x\r\n"       // another host
        "file:///a/../etc/passwd\r\n"        // a parent segment
        "file:///a/%2e%2e/b\r\n"             // the same, percent-encoded
        "relative/path\r\n"                  // not a file: URI
        "file://relative\r\n"                // no absolute path
        "file:///bad%00nul\r\n"              // an encoded NUL
        "file:///bad%1Fcontrol\r\n"          // an encoded control byte
        "file:///truncated%2\r\n"            // a broken escape
        "file:///ok/fine.txt\r\n",
        &rejected);
    CHECK(Equal(out, "/ok/fine.txt"));
    CHECK(rejected == 9);
}

TEST_CASE("Clipboard: a file list built from paths parses back to the same paths", "[clipboard][os]") {
    const Vector<byte> paths = Bytes("/home/user/a b.txt\n/tmp/\xC3\xA9t\xC3\xA9 #1.png\n/x/100%/y");
    Vector<byte> list;
    BuildUriList(paths.Data(), paths.Size(), list);
    CHECK(list.Size() > paths.Size());
    Vector<byte> back;
    u32 rejected = 0;
    ParseUriList(list.Data(), list.Size(), back, &rejected);
    CHECK(rejected == 0);
    CHECK(Equal(back, paths));
}

TEST_CASE("Clipboard: requests complete once, time out, and run at most eight at a time", "[clipboard][os]") {
    InputEventQueue queue;
    ClipboardRequests requests;
    requests.Init(&queue);

    const u32 a = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 0);
    const u32 b = requests.Begin(ClipboardKind::Primary, ClipboardFormat::FileList, 0);
    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(a != b);
    CHECK(requests.InFlight() == 2);
    CHECK(requests.Pending(a));

    CHECK(requests.Complete(a, ClipboardStatus::Ok, Bytes("hi"), 0, 0, kSecond / 2));
    CHECK_FALSE(requests.Complete(a, ClipboardStatus::Failed, Vector<byte>(), 0, 0, kSecond / 2)); // only once
    CHECK_FALSE(requests.Pending(a));

    InputEvent event;
    REQUIRE(queue.Pop(&event));
    CHECK(event.type == InputEventType::ClipboardData);
    CHECK(event.request_id == a);
    CHECK(event.clipboard_status == static_cast<u8>(ClipboardStatus::Ok));

    ClipboardResult result;
    REQUIRE(requests.Take(a, &result));
    CHECK(Equal(result.bytes, "hi"));
    CHECK_FALSE(requests.Take(a, &result)); // taken once

    // b hears nothing: just before the timeout it is open, at the timeout it ends with Timeout.
    CHECK(requests.Expire(kClipboardTimeoutNs - 1) == 0);
    CHECK(requests.Expire(kClipboardTimeoutNs) == 1);
    REQUIRE(requests.Take(b, &result));
    CHECK(result.status == ClipboardStatus::Timeout);
    CHECK(result.kind == ClipboardKind::Primary);

    // Progress restarts the clock.
    const u32 c = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 10 * kSecond);
    requests.Touch(c, 11 * kSecond);
    CHECK(requests.Expire(12 * kSecond) == 0);
    CHECK(requests.Expire(13 * kSecond + 1) == 1);

    // The ninth request in flight is refused.
    u32 ids[8];
    for (u32& id : ids) {
        id = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 20 * kSecond);
        CHECK(id != 0);
    }
    CHECK(requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 20 * kSecond) == 0);
    requests.FailAll(ClipboardStatus::Failed, 21 * kSecond);
    CHECK(requests.InFlight() == 0);
}

TEST_CASE("Clipboard: two requests in flight finish independently and out of order", "[clipboard][os]") {
    InputEventQueue queue;
    ClipboardRequests requests;
    requests.Init(&queue);
    const u32 a = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 0);
    const u32 b = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Custom, 0);
    CHECK(requests.Complete(b, ClipboardStatus::Ok, Bytes("second"), 0, 0, 1));
    CHECK(requests.Pending(a));
    CHECK(requests.Complete(a, ClipboardStatus::Missing, Vector<byte>(), 0, 0, 2));
    ClipboardResult ra;
    ClipboardResult rb;
    REQUIRE(requests.Take(a, &ra));
    REQUIRE(requests.Take(b, &rb));
    CHECK(ra.status == ClipboardStatus::Missing);
    CHECK(rb.format == ClipboardFormat::Custom);
    CHECK(Equal(rb.bytes, "second"));
}

TEST_CASE("Clipboard: unread results are bounded: the oldest is dropped", "[clipboard][os]") {
    InputEventQueue queue;
    ClipboardRequests requests;
    requests.Init(&queue);
    u32 first = 0;
    for (u32 i = 0; i < ClipboardRequests::kMaxUnread + 1; ++i) {
        const u32 id = requests.Begin(ClipboardKind::Clipboard, ClipboardFormat::Text, 0);
        if (i == 0) first = id;
        requests.Complete(id, ClipboardStatus::Ok, Bytes("x"), 0, 0, i);
    }
    ClipboardResult result;
    CHECK_FALSE(requests.Take(first, &result));
}

// ---- pipes ----------------------------------------------------------------------------------------------------------

namespace {

Vector<byte> Pattern(usize size) {
    Vector<byte> out;
    for (usize i = 0; i < size; ++i) out.Push(static_cast<byte>((i * 31 + 7) & 0xFF));
    return out;
}

} // namespace

TEST_CASE("Clipboard: a 5 MiB transfer through a pipe the peer drains slowly arrives whole", "[clipboard][pipe][os]") {
    int fds[2];
    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    const Vector<byte> data = Pattern(5 * 1024 * 1024);
    PipeWriter writer;
    writer.Start(fds[1], data.Data(), data.Size(), 0, kClipboardTimeoutNs);
    PipeReader reader;
    reader.Start(fds[0], kMaxClipboardBytes, 0, kClipboardTimeoutNs);

    // The writer alone fills the pipe (64 KiB) and stalls: still Writing, nothing blocks.
    CHECK(writer.Pump(1) == PipeWriter::State::Writing);
    u64 now = 2;
    for (int i = 0; i < 100000 && (writer.GetState() == PipeWriter::State::Writing || reader.GetState() == PipeReader::State::Reading); ++i) {
        now += 1000;
        if (writer.GetState() == PipeWriter::State::Writing) writer.Pump(now);
        if (writer.GetState() != PipeWriter::State::Writing) { /* closed by Pump on Done */ }
        reader.Pump(now);
    }
    CHECK(writer.GetState() == PipeWriter::State::Done);
    CHECK(reader.GetState() == PipeReader::State::Done);
    CHECK(Equal(reader.Bytes(), data));
}

TEST_CASE("Clipboard: a pipe whose peer never reads times out without blocking", "[clipboard][pipe][os]") {
    int fds[2];
    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    const Vector<byte> data = Pattern(1024 * 1024);
    PipeWriter writer;
    writer.Start(fds[1], data.Data(), data.Size(), 0, kClipboardTimeoutNs);
    CHECK(writer.Pump(1) == PipeWriter::State::Writing);
    CHECK(writer.Pump(kSecond) == PipeWriter::State::Writing);
    CHECK(writer.Pump(kClipboardTimeoutNs + 2) == PipeWriter::State::TimedOut);
    writer.Close();
    ::close(fds[0]);
}

TEST_CASE("Clipboard: a pipe whose peer closes early fails the writer and ends the reader", "[clipboard][pipe][os]") {
    int fds[2];
    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    const Vector<byte> data = Pattern(1024 * 1024);
    PipeWriter writer;
    writer.Start(fds[1], data.Data(), data.Size(), 0, kClipboardTimeoutNs);
    ::close(fds[0]); // the reader hangs up (SIGPIPE must not kill the process)
    PipeWriter::State state = writer.Pump(1);
    CHECK(state == PipeWriter::State::Failed);
    writer.Close();

    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    PipeReader reader;
    reader.Start(fds[0], 100, 0, kClipboardTimeoutNs);
    CHECK(::write(fds[1], "abc", 3) == 3);
    ::close(fds[1]); // the writer hangs up after three bytes: that is the whole answer
    CHECK(reader.Pump(1) == PipeReader::State::Done);
    CHECK(Equal(reader.Bytes(), "abc"));
}

TEST_CASE("Clipboard: a pipe reader refuses more than its cap and times out when the peer is silent", "[clipboard][pipe][os]") {
    int fds[2];
    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    PipeReader reader;
    reader.Start(fds[0], 10, 0, kClipboardTimeoutNs);
    const Vector<byte> data = Pattern(20);
    CHECK(::write(fds[1], data.Data(), data.Size()) == 20);
    CHECK(reader.Pump(1) == PipeReader::State::TooLarge);
    reader.Close();
    ::close(fds[1]);

    REQUIRE(::pipe2(fds, O_CLOEXEC) == 0);
    PipeReader silent;
    silent.Start(fds[0], 10, 0, kClipboardTimeoutNs);
    CHECK(silent.Pump(kSecond) == PipeReader::State::Reading);
    CHECK(silent.Pump(kClipboardTimeoutNs + 1) == PipeReader::State::TimedOut);
    silent.Close();
    ::close(fds[1]);
}

TEST_CASE("Clipboard: every listener slot is filled on Wayland", "[clipboard][wayland][os]") {
    CHECK(wayland::WaylandClipboard::ListenersComplete());
}

// There is no test compositor, and a compositor accepts a selection only with the serial of a real key or button event, which a test cannot produce: these cases check what
// can be checked without one (the requests end, the protocol is not violated, our own selection answers locally).
TEST_CASE("Wayland: a request for a format nobody offers ends with Missing and the connection survives", "[clipboard][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    wayland::WaylandPlatformWindow window;
    if (!window.Open("clipboard test", 320, 240, &input)) SKIP("no Wayland session");
    if (!window.SupportsClipboard(ClipboardKind::Clipboard)) SKIP("the compositor does not offer wl_data_device_manager");
    const u32 id = window.RequestClipboard(ClipboardKind::Clipboard, ClipboardFormat::Custom, "application/x-engine-nobody-offers-this");
    REQUIRE(id != 0);
    InputEvent event;
    bool got = false;
    for (int i = 0; i < 300 && !got; ++i) {
        window.PumpEvents();
        while (window.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardData && event.request_id == id) got = true;
        }
        SleepNanos(10'000'000);
    }
    CHECK(got);
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Missing);
    CHECK(window.Valid());
}

TEST_CASE("Wayland: our own selection answers a request locally, when the compositor accepted it", "[clipboard][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    wayland::WaylandPlatformWindow window;
    if (!window.Open("clipboard test", 320, 240, &input)) SKIP("no Wayland session");
    if (!window.SupportsClipboard(ClipboardKind::Clipboard)) SKIP("the compositor does not offer wl_data_device_manager");
    for (int i = 0; i < 20; ++i) window.PumpEvents();
    const Vector<byte> text = Bytes("caf\xC3\xA9 \xF0\x9F\x8E\x89");
    ClipboardOffer offer;
    offer.data = text.Data();
    offer.size = text.Size();
    if (!window.SetClipboard(ClipboardKind::Clipboard, offer)) SKIP("no input serial yet: the window never got keyboard or pointer focus");
    CHECK(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()));
    CHECK_FALSE(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
    const u32 id = window.RequestClipboard(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView());
    REQUIRE(id != 0);
    window.PumpEvents();
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, text));
}

// ---- X11: two connections ----------------------------------------------------------------------------------------------

namespace {

using engine_placeholder::os::x11::X11Window;

// Pumps both windows until `done()` or ~3 s pass.
template <typename Pred>
bool PumpBoth(X11Window& a, X11Window& b, Pred done) {
    for (int i = 0; i < 300; ++i) {
        a.PumpEvents(nullptr);
        b.PumpEvents(nullptr);
        if (done()) return true;
        SleepNanos(10'000'000);
    }
    return false;
}

// Requests `format` from `reader` and waits for the result.
bool Fetch(X11Window& owner, X11Window& reader, ClipboardKind kind, ClipboardFormat format, StringView name, ClipboardResult* out) {
    const u32 id = reader.RequestClipboard(kind, format, name);
    if (id == 0) return false;
    InputEvent event;
    bool got = false;
    PumpBoth(owner, reader, [&] {
        while (reader.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardData && event.request_id == id) got = true;
        }
        return got;
    });
    return got && reader.TakeClipboardResult(id, out);
}

void Offer(X11Window& window, ClipboardKind kind, const Vector<byte>& text) {
    ClipboardOffer offer;
    offer.format = ClipboardFormat::Text;
    offer.data = text.Data();
    offer.size = text.Size();
    REQUIRE(window.SetClipboard(kind, offer));
}

} // namespace

TEST_CASE("X11: text set on one connection is read on another, for every kind of text", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());
    CHECK(owner.SupportsClipboard(ClipboardKind::Clipboard));
    CHECK(owner.SupportsClipboard(ClipboardKind::Primary));

    const char* samples[] = {"plain ascii", "caf\xC3\xA9 na\xC3\xAFve", "e\xCC\x81 combining", "party \xF0\x9F\x8E\x89!", "snow \xE2\x98\x83", ""};
    for (ClipboardKind kind : {ClipboardKind::Clipboard, ClipboardKind::Primary}) {
        for (const char* sample : samples) {
            Offer(owner, kind, Bytes(sample));
            ClipboardResult result;
            REQUIRE(Fetch(owner, reader, kind, ClipboardFormat::Text, StringView(), &result));
            CHECK(result.status == ClipboardStatus::Ok);
            CHECK(result.kind == kind);
            CHECK(Equal(result.bytes, sample));
            CHECK(result.replaced_sequences == 0);
        }
    }
}

TEST_CASE("X11: 5 MiB travels in pieces (INCR) and arrives whole", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());
    Vector<byte> big;
    for (usize i = 0; i < 5 * 1024 * 1024; ++i) big.Push(static_cast<byte>('a' + (i * 7) % 26));
    Offer(owner, ClipboardKind::Clipboard, big);
    ClipboardResult result;
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::Text, StringView(), &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, big));
}

TEST_CASE("X11: a format nobody offered is Missing, and an empty selection too", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());
    Offer(owner, ClipboardKind::Clipboard, Bytes("text only"));
    // Asked at once: a desktop's clipboard manager (Klipper) may take the selection over a moment later, and then it is no longer ours to answer for.
    CHECK(owner.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()));
    CHECK_FALSE(owner.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
    ClipboardResult result;
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView(), &result));
    CHECK(result.status == ClipboardStatus::Missing);
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::Custom, "application/x-no-such", &result));
    CHECK(result.status == ClipboardStatus::Missing);
}

TEST_CASE("X11: a file list and a custom format round trip", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());

    const Vector<byte> paths = Bytes("/tmp/a b.txt\n/home/user/\xC3\xA9t\xC3\xA9.png");
    ClipboardOffer files;
    files.format = ClipboardFormat::FileList;
    files.data = paths.Data();
    files.size = paths.Size();
    REQUIRE(owner.SetClipboard(ClipboardKind::Clipboard, files));
    ClipboardResult result;
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView(), &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, paths));

    const Vector<byte> blob = Raw({0, 1, 2, 3, 255, 254});
    ClipboardOffer custom;
    custom.format = ClipboardFormat::Custom;
    custom.custom_name = "application/x-engine-test";
    custom.data = blob.Data();
    custom.size = blob.Size();
    REQUIRE(owner.SetClipboard(ClipboardKind::Clipboard, custom));
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::Custom, "application/x-engine-test", &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, blob));
}

TEST_CASE("X11: losing the selection to another window queues ClipboardChanged on the loser", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window first = X11Window::Create("clipboard first", 200, 100);
    X11Window second = X11Window::Create("clipboard second", 200, 100);
    REQUIRE(first.Valid());
    REQUIRE(second.Valid());
    Offer(first, ClipboardKind::Clipboard, Bytes("one"));
    Offer(second, ClipboardKind::Clipboard, Bytes("two"));
    bool changed = false;
    InputEvent event;
    PumpBoth(first, second, [&] {
        while (first.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardChanged && event.clipboard_kind == static_cast<u8>(ClipboardKind::Clipboard)) changed = true;
        }
        return changed;
    });
    CHECK(changed);
    CHECK_FALSE(first.ClipboardHasFormat(ClipboardKind::Primary, ClipboardFormat::Text, StringView()));
    ClipboardResult result;
    REQUIRE(Fetch(second, first, ClipboardKind::Clipboard, ClipboardFormat::Text, StringView(), &result));
    CHECK(Equal(result.bytes, "two"));
}

TEST_CASE("X11: when the owner goes away mid-request the request still ends", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(reader.Valid());
    u32 id = 0;
    {
        X11Window owner = X11Window::Create("clipboard owner", 200, 100);
        REQUIRE(owner.Valid());
        Offer(owner, ClipboardKind::Clipboard, Bytes("soon gone"));
        id = reader.RequestClipboard(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView());
        REQUIRE(id != 0);
    } // the owner never pumped: it dies without answering
    InputEvent event;
    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        reader.PumpEvents(nullptr);
        while (reader.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardData && event.request_id == id) got = true;
        }
        SleepNanos(10'000'000);
    }
    CHECK(got);
    ClipboardResult result;
    REQUIRE(reader.TakeClipboardResult(id, &result));
    CHECK(result.status != ClipboardStatus::Ok);
}

TEST_CASE("X11: two requests in flight both complete with their own data", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());
    Offer(owner, ClipboardKind::Clipboard, Bytes("clip"));
    Offer(owner, ClipboardKind::Primary, Bytes("prim"));
    const u32 a = reader.RequestClipboard(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView());
    const u32 b = reader.RequestClipboard(ClipboardKind::Primary, ClipboardFormat::Text, StringView());
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    REQUIRE(a != b);
    u32 done = 0;
    InputEvent event;
    PumpBoth(owner, reader, [&] {
        while (reader.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardData) ++done;
        }
        return done == 2;
    });
    ClipboardResult ra;
    ClipboardResult rb;
    REQUIRE(reader.TakeClipboardResult(a, &ra));
    REQUIRE(reader.TakeClipboardResult(b, &rb));
    CHECK(Equal(ra.bytes, "clip"));
    CHECK(Equal(rb.bytes, "prim"));
}

TEST_CASE("X11: invalid UTF-8 from an owner is cleaned and counted", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    X11Window reader = X11Window::Create("clipboard reader", 200, 100);
    REQUIRE(owner.Valid());
    REQUIRE(reader.Valid());
    // SetClipboard cleans text it is given, so the owner's side is clean; the reader's cleaning is covered by the pure tests. Here: the cleaned text arrives with no loss.
    Offer(owner, ClipboardKind::Clipboard, Raw({'o', 'k', 0xFF, '!'}));
    ClipboardResult result;
    REQUIRE(Fetch(owner, reader, ClipboardKind::Clipboard, ClipboardFormat::Text, StringView(), &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, "ok\xEF\xBF\xBD!"));
    CHECK(result.replaced_sequences == 0);
}

TEST_CASE("X11: a window opened after another program took the clipboard knows its formats", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    REQUIRE(owner.Valid());
    Offer(owner, ClipboardKind::Clipboard, Bytes("already there"));
    X11Window late = X11Window::Create("clipboard late", 200, 100);
    REQUIRE(late.Valid());
    // A desktop clipboard manager (Klipper) may take the selection over meanwhile, and then the answer is its own (or a refusal): the check is that either the formats are
    // known, or the takeover was announced with ClipboardChanged.
    bool changed = false;
    InputEvent event;
    PumpBoth(owner, late, [&] {
        while (late.PollEvent(&event)) {
            if (event.type == InputEventType::ClipboardChanged) changed = true;
        }
        return late.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView());
    });
    CHECK((late.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()) || changed));
    CHECK_FALSE(late.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
}

TEST_CASE("X11: a window that cannot own (bad offer) reports failure", "[clipboard][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("clipboard bad", 200, 100);
    REQUIRE(window.Valid());
    ClipboardOffer custom;
    custom.format = ClipboardFormat::Custom; // no name
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, custom));
    ClipboardOffer broken;
    broken.size = 4; // bytes missing
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, broken));
    CHECK(window.RequestClipboard(ClipboardKind::Clipboard, ClipboardFormat::Custom, StringView()) == 0);
}

} // namespace window_clipboard_test

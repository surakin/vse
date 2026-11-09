// The input probe (`input_probe`): prints the window's input event stream and lets a person try the cursor features by eye.
//   L  toggle the pointer lock (the cursor hides and RelativeMotion events appear)
//   C  cycle the standard cursor shapes
//   I  toggle a generated custom cursor image (a 32x32 ring with the hotspot in the middle)
//   Y  copy a test string ("caf\xC3\xA9 \xF0\x9F\x8E\x89 from input_probe") to the clipboard; paste it into another program to check
//   P  paste: request the clipboard text and print it when it arrives (copy something in another program first)
//   O  like P for the primary selection (the middle-click one)
//   F  show which formats the clipboard offers
//   D  start a drag of a test string (drop it into another program); drops on this window are printed (text and files accepted)
//   Esc quit
// Options: --backend=wayland|x11|auto, --run-seconds=N (default 60).
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/os.h"
#include "platform/cursor.h"
#include "platform/drag_drop.h"
#include "platform/entry_point.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/window_host.h"
#include "string/string.h"
#include "containers/vector.h"

using namespace engine_placeholder;

namespace {

const char* TypeName(os::InputEventType type) {
    switch (type) {
    case os::InputEventType::KeyDown: return "KeyDown";
    case os::InputEventType::KeyUp: return "KeyUp";
    case os::InputEventType::Text: return "Text";
    case os::InputEventType::PointerMove: return "PointerMove";
    case os::InputEventType::PointerButton: return "PointerButton";
    case os::InputEventType::Wheel: return "Wheel";
    case os::InputEventType::PointerEnter: return "PointerEnter";
    case os::InputEventType::PointerLeave: return "PointerLeave";
    case os::InputEventType::FocusGained: return "FocusGained";
    case os::InputEventType::FocusLost: return "FocusLost";
    case os::InputEventType::Resize: return "Resize";
    case os::InputEventType::RelativeMotion: return "RelativeMotion";
    case os::InputEventType::ClipboardData: return "ClipboardData";
    case os::InputEventType::ClipboardChanged: return "ClipboardChanged";
    case os::InputEventType::DragEnter: return "DragEnter";
    case os::InputEventType::DragOver: return "DragOver";
    case os::InputEventType::DragLeave: return "DragLeave";
    case os::InputEventType::Drop: return "Drop";
    case os::InputEventType::DropData: return "DropData";
    case os::InputEventType::DragEnded: return "DragEnded";
    default: return "None";
    }
}

void Print(const os::InputEvent& e) {
    ArenaAllocator scratch(512);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append(TypeName(e.type));
    switch (e.type) {
    case os::InputEventType::KeyDown:
    case os::InputEventType::KeyUp: line.Append(" key=").AppendUInt(static_cast<u32>(e.key)).Append(e.repeat ? " repeat" : ""); break;
    case os::InputEventType::Text: line.Append(" \"").Append(StringView(e.text, e.text_length)).Append('"'); break;
    case os::InputEventType::PointerButton: line.Append(" button=0x").AppendHex(e.button).Append(e.down ? " down" : " up"); break;
    case os::InputEventType::ClipboardData: line.Append(" id=").AppendUInt(e.request_id).Append(" status=").AppendUInt(e.clipboard_status); break;
    case os::InputEventType::DragEnter:
    case os::InputEventType::DragOver:
    case os::InputEventType::Drop: line.Append(" formats=").AppendUInt(e.drag_formats).Append(" actions=").AppendUInt(e.drag_actions).Append(" action=").AppendUInt(e.drag_action).Append(" at (").AppendFloat(e.x, 1).Append(", ").AppendFloat(e.y, 1).Append(')'); break;
    case os::InputEventType::DropData: line.Append(" id=").AppendUInt(e.request_id).Append(" status=").AppendUInt(e.clipboard_status); break;
    case os::InputEventType::DragEnded: line.Append(" action=").AppendUInt(e.drag_action); break;
    case os::InputEventType::DragLeave: break;
    case os::InputEventType::ClipboardChanged: line.Append(" kind=").AppendUInt(e.clipboard_kind); break;
    case os::InputEventType::Resize: line.Append(' ').AppendUInt(e.width).Append('x').AppendUInt(e.height); break;
    case os::InputEventType::None:
    case os::InputEventType::PointerLeave:
    case os::InputEventType::FocusGained:
    case os::InputEventType::FocusLost: break;
    default: line.Append(" (").AppendFloat(e.x, 2).Append(", ").AppendFloat(e.y, 2).Append(')'); break;
    }
    line.Append('\n');
    os::Write(1, line.CStr(), line.View().Length());
}

void Say(const char* text) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("probe: ").Append(text).Append('\n');
    os::Write(1, line.CStr(), line.View().Length());
}

// A 32x32 ring, opaque white with a dark edge, premultiplied RGBA.
void MakeRing(Vector<byte>& pixels) {
    constexpr int n = 32;
    pixels.Resize(static_cast<usize>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const f64 dx = x - 15.5;
            const f64 dy = y - 15.5;
            const f64 r2 = dx * dx + dy * dy;
            byte* p = &pixels[(static_cast<usize>(y) * n + x) * 4];
            byte v = 0;
            byte a = 0;
            if (r2 <= 15.5 * 15.5 && r2 >= 10.0 * 10.0) {
                a = 255;
                v = r2 > 13.5 * 13.5 || r2 < 11.5 * 11.5 ? 20 : 255;
            }
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = a;
        }
    }
}

} // namespace

int EngineMain(int, char**) {
    ArenaAllocator cmd_arena(4096);
    os::CommandLine<ArenaAllocator> cmd = os::CommandLine<ArenaAllocator>::FromNative(cmd_arena);
    LogPlatform.SetVerbosity(LogVerbosity::Info);

    os::InputState input;
    os::PlatformWindowHost host;
    os::WindowBackend backend = os::WindowBackend::Invalid;
    for (usize i = 0; i < cmd.FlagCount(); ++i) {
        if (cmd.FlagName(i) == StringView("backend") && cmd.FlagHasValue(i)) backend = os::ParseWindowBackend(cmd.FlagValue(i));
    }
    const bool opened = backend == os::WindowBackend::Invalid ? host.OpenFromEnvironment("input probe", 800, 600, &input) : host.Open(backend, "input probe", 800, 600, &input);
    os::Window* window = host.Get();
    if (!opened || window == nullptr) {
        Say("no window could be opened");
        return 1;
    }
    u64 run_seconds = 60;
    for (usize i = 0; i < cmd.FlagCount(); ++i) {
        if (cmd.FlagName(i) == StringView("run-seconds") && cmd.FlagHasValue(i)) {
            u64 v = 0;
            const StringView text = cmd.FlagValue(i);
            for (usize k = 0; k < text.Length(); ++k) v = v * 10 + static_cast<u64>(text[k] - '0');
            run_seconds = v;
        }
    }
    {
        ArenaAllocator scratch(256);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("backend ").Append(window->BackendName()).Append(": shapes=").AppendUInt(window->SupportsCursorShape()).Append(" image=").AppendUInt(window->SupportsCursorImage());
        line.Append(" lock=").AppendUInt(window->SupportsPointerLock()).Append("  keys: L lock, C shape, I image, Y copy, P paste, O paste primary, F formats, D drag, Esc quit\n");
        os::Write(1, line.CStr(), line.View().Length());
    }

    if (window->SupportsDragAndDrop()) {
        os::DropAccept accept;
        accept.formats = os::kDropText | os::kDropFiles;
        window->SetDropAccept(accept);
        window->SetDropAction(os::DragAction::Copy);
    }
    u32 drop_text_id = 0;
    u32 drop_files_id = 0;

    Vector<byte> ring;
    MakeRing(ring);
    os::CursorImage image;
    image.rgba = ring.Data();
    image.width = 32;
    image.height = 32;
    image.hotspot_x = 16;
    image.hotspot_y = 16;

    // The text of the last ClipboardData result is printed by the key handler below once the event arrives.
    u32 paste_id = 0;
    os::ClipboardKind paste_kind = os::ClipboardKind::Clipboard;
    bool locked = false;
    bool image_on = false;
    u32 shape = 0;
    const u64 start = os::MonotonicTimeNanos();
    while (os::MonotonicTimeNanos() - start < run_seconds * 1'000'000'000ull && !window->ShouldClose()) {
        input.BeginFrame();
        window->PumpEvents();
        os::InputEvent e;
        while (window->PollEvent(&e)) {
            Print(e);
            if (e.type == os::InputEventType::ClipboardData && e.request_id == paste_id) {
                os::ClipboardResult result;
                if (window->TakeClipboardResult(e.request_id, &result) && result.status == os::ClipboardStatus::Ok) {
                    Say("pasted text:");
                    os::Write(1, result.bytes.Data(), result.bytes.Size());
                    os::Write(1, "\n", 1);
                    if (result.replaced_sequences != 0) Say("(invalid UTF-8 was replaced)");
                } else {
                    Say("paste failed: nothing there, no text format, or nobody answered");
                }
                paste_id = 0;
            }
            if (e.type == os::InputEventType::Drop) {
                if ((e.drag_formats & os::kDropFiles) != 0) drop_files_id = window->RequestDropData(os::ClipboardFormat::FileList, StringView());
                if ((e.drag_formats & os::kDropText) != 0) drop_text_id = window->RequestDropData(os::ClipboardFormat::Text, StringView());
                if (drop_files_id == 0 && drop_text_id == 0) window->FinishDrop(os::DragAction::Nothing);
            }
            if (e.type == os::InputEventType::DropData) {
                os::ClipboardResult result;
                if (window->TakeClipboardResult(e.request_id, &result) && result.status == os::ClipboardStatus::Ok) {
                    Say(e.request_id == drop_files_id ? "dropped files:" : "dropped text:");
                    os::Write(1, result.bytes.Data(), result.bytes.Size());
                    os::Write(1, "\n", 1);
                    if (result.rejected_entries != 0) Say("(some entries were rejected)");
                } else {
                    Say("reading the dropped data failed");
                }
                if (e.request_id == drop_files_id) drop_files_id = 0;
                if (e.request_id == drop_text_id) drop_text_id = 0;
                if (drop_files_id == 0 && drop_text_id == 0) window->FinishDrop(os::DragAction::Copy);
            }
            if (e.type != os::InputEventType::KeyDown || e.repeat) continue;
            if (e.key == os::Key::Escape) return 0;
            if (e.key == os::Key::L) {
                locked = !locked;
                window->SetPointerLock(locked);
                Say(locked ? "pointer lock requested" : "pointer lock released");
                window->SetCursor(locked ? os::CursorShape::Hidden : static_cast<os::CursorShape>(shape));
            } else if (e.key == os::Key::C) {
                shape = (shape + 1) % static_cast<u32>(os::CursorShape::Count);
                const bool ok = window->SetCursor(static_cast<os::CursorShape>(shape));
                ArenaAllocator scratch(128);
                StringBuilder<ArenaAllocator> line(scratch);
                line.Append("cursor shape ").AppendUInt(shape).Append(ok ? " set" : " refused").Append('\n');
                os::Write(1, line.CStr(), line.View().Length());
            } else if (e.key == os::Key::Y) {
                static const char kText[] = "caf\xC3\xA9 \xF0\x9F\x8E\x89 from input_probe";
                os::ClipboardOffer offer;
                offer.data = reinterpret_cast<const byte*>(kText);
                offer.size = sizeof(kText) - 1;
                const bool ok = window->SetClipboard(os::ClipboardKind::Clipboard, offer);
                window->SetClipboard(os::ClipboardKind::Primary, offer);
                Say(ok ? "copied: paste it into another program" : "copy refused (Wayland needs a recent key press in this window)");
            } else if (e.key == os::Key::P || e.key == os::Key::O) {
                paste_kind = e.key == os::Key::P ? os::ClipboardKind::Clipboard : os::ClipboardKind::Primary;
                paste_id = window->RequestClipboard(paste_kind, os::ClipboardFormat::Text, StringView());
                Say(paste_id != 0 ? "paste requested" : "paste not possible");
            } else if (e.key == os::Key::F) {
                for (os::ClipboardKind kind : {os::ClipboardKind::Clipboard, os::ClipboardKind::Primary}) {
                    ArenaAllocator scratch(256);
                    StringBuilder<ArenaAllocator> line(scratch);
                    line.Append(kind == os::ClipboardKind::Clipboard ? "clipboard" : "primary").Append(": text=").AppendUInt(window->ClipboardHasFormat(kind, os::ClipboardFormat::Text, StringView()));
                    line.Append(" files=").AppendUInt(window->ClipboardHasFormat(kind, os::ClipboardFormat::FileList, StringView())).Append('\n');
                    os::Write(1, line.CStr(), line.View().Length());
                }
            } else if (e.key == os::Key::D) {
                static const char kDrag[] = "dragged from input_probe \xF0\x9F\x8E\x89";
                os::ClipboardOffer offer;
                offer.data = reinterpret_cast<const byte*>(kDrag);
                offer.size = sizeof(kDrag) - 1;
                os::DragSource source;
                source.offers = &offer;
                source.offer_count = 1;
                source.allowed_actions = os::kAllDragActions;
                Say(window->BeginDrag(source) ? "drag started: move the pointer and release over another program" : "drag refused (hold a button first on Wayland, or one is running)");
            } else if (e.key == os::Key::I) {
                image_on = !image_on;
                if (image_on) {
                    Say(window->SetCursorImage(image) ? "custom cursor image set" : "custom cursor image refused");
                } else {
                    window->ClearCursorImage();
                    Say("custom cursor image cleared");
                }
            }
        }
        os::SleepNanos(8'000'000);
    }
    return 0;
}

ENGINE_WINDOWED_PROGRAM(EngineMain)

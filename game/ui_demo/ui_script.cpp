#include "ui_script.h"

#include "cstr/format.h"
#include "cstr/number.h"
#include "cstr/cstr.h"

#include "platform/drag_drop.h"
#include "platform/input.h"
#include "ui/drag_drop.h"

namespace ui_demo
{

using namespace engine_placeholder::ui;

namespace
{

String<HeapAllocator> OwnedWord(StringView text)
{
    return String<HeapAllocator>(HeapAllocator::Default(), text);
}

bool StartsWithWord(StringView word, const char* prefix)
{
    const usize n = CStrLength(prefix);
    if (word.Length() < n)
    {
        return false;
    }
    for (usize i = 0; i < n; ++i)
    {
        if (word[i] != prefix[i])
        {
            return false;
        }
    }
    return true;
}

// The text after `prefix`.
StringView After(StringView word, const char* prefix)
{
    return word.Substring(CStrLength(prefix));
}

bool SameName(StringView a, const char* b)
{
    const usize n = CStrLength(b);
    if (a.Length() != n)
    {
        return false;
    }
    for (usize i = 0; i < n; ++i)
    {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z')
        {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z')
        {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

// A key name to a key; returns false for an unknown one.
bool ParseKey(StringView name, Key* key)
{
    if (name.Length() == 1 && name[0] >= 'a' && name[0] <= 'z')
    {
        *key = static_cast<Key>(static_cast<u8>(Key::A) + (name[0] - 'a'));
        return true;
    }
    if (name.Length() == 1 && name[0] >= 'A' && name[0] <= 'Z')
    {
        *key = static_cast<Key>(static_cast<u8>(Key::A) + (name[0] - 'A'));
        return true;
    }
    if (name.Length() >= 2 && (name[0] == 'F' || name[0] == 'f'))
    {
        char digits[8] = {};
        for (usize i = 1; i < name.Length() && i < 4; ++i)
        {
            digits[i - 1] = name[i];
        }
        const int n = CStrToInt(digits);
        if (n >= 1 && n <= 12)
        {
            *key = static_cast<Key>(static_cast<u8>(Key::F1) + (n - 1));
            return true;
        }
    }
    struct Named
    {
        const char* name;
        Key key;
    };
    static const Named kNamed[] = {{"enter", Key::Enter},   {"tab", Key::Tab},           {"escape", Key::Escape},
                                   {"space", Key::Space},   {"left", Key::Left},         {"right", Key::Right},
                                   {"up", Key::Up},         {"down", Key::Down},         {"home", Key::Home},
                                   {"end", Key::End},       {"delete", Key::Delete},     {"backspace", Key::Backspace},
                                   {"pageup", Key::PageUp}, {"pagedown", Key::PageDown}, {"grave", Key::Grave}};
    for (const Named& n : kNamed)
    {
        if (SameName(name, n.name))
        {
            *key = n.key;
            return true;
        }
    }
    return false;
}

ui::Point CenterOf(const Widget* w)
{
    const Rect& b = w->Bounds();
    return ui::Point{b.x + b.w / 2, b.y + b.h / 2};
}

void PostPointer(Surface* s, EventType type, ui::Point p, u32 button)
{
    Event e;
    e.type = type;
    e.position = p;
    e.button = button;
    s->Post(e);
}

}  // namespace

usize UiScript::Parse(StringView text)
{
    words_.Clear();
    next_ = 0;
    wait_until_ = 0;
    usize i = 0;
    while (i < text.Length())
    {
        while (i < text.Length() && text[i] == ' ')
        {
            ++i;
        }
        const usize start = i;
        while (i < text.Length() && text[i] != ' ')
        {
            ++i;
        }
        if (i > start)
        {
            words_.Push(OwnedWord(text.Substring(start, i - start)));
        }
    }
    return words_.Size();
}

bool UiScript::Run(UiDemoApp* app, Surface* surface, ScriptHost* host, long frame)
{
    while (next_ < words_.Size() && frame >= wait_until_)
    {
        const String<HeapAllocator>& word = words_[next_++];
        if (StartsWithWord(word.View(), "frame:"))
        {
            wait_until_ = CStrToLong(word.CStr() + 6);
            continue;
        }
        if (!RunWord(word.View(), app, surface, host))
        {
            char text[256];
            const int n = FormatTo(text, sizeof(text), "script: cannot do '%s'", word.CStr());
            error_ = OwnedWord(StringView(text, FittedLength(n, sizeof(text))));
            return false;
        }
    }
    return true;
}

bool UiScript::RunWord(StringView word, UiDemoApp* app, Surface* surface, ScriptHost* host)
{
    if (SameName(word, "quit"))
    {
        app->RequestQuit();
        return true;
    }
    if (SameName(word, "rendering"))
    {
        app->ShowRenderingSettings();
        return true;
    }
    if (SameName(word, "context"))
    {
        Widget* panel = app->Find(StringView("left_panel"));
        if (panel == nullptr)
        {
            return false;
        }
        app->ShowContext(CenterOf(panel));
        return true;
    }
    if (StartsWithWord(word, "click:"))
    {
        Widget* w = app->Find(After(word, "click:"));
        if (w == nullptr)
        {
            return false;
        }
        const ui::Point p = CenterOf(w);
        PostPointer(surface, EventType::PointerMove, p, 0);
        PostPointer(surface, EventType::PointerDown, p, os::kMouseLeft);
        PostPointer(surface, EventType::PointerUp, p, os::kMouseLeft);
        return true;
    }
    if (StartsWithWord(word, "type:"))
    {
        String<HeapAllocator> text = OwnedWord(After(word, "type:"));
        usize i = 0;
        const StringView utf8 = text.View();
        while (i < utf8.Length())
        {
            const u8 lead = static_cast<u8>(utf8[i]);
            usize len = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
            if (i + len > utf8.Length())
            {
                len = utf8.Length() - i;
            }
            Event e;
            e.type = EventType::Text;
            e.text_length = static_cast<u8>(len);
            for (usize k = 0; k < len && k < 4; ++k)
            {
                e.text[k] = utf8[i + k] == '_' ? ' ' : utf8[i + k];
            }
            surface->Post(e);
            i += len;
        }
        return true;
    }
    if (StartsWithWord(word, "key:"))
    {
        StringView name = After(word, "key:");
        u32 modifiers = 0;
        for (;;)
        {
            if (StartsWithWord(name, "ctrl+"))
            {
                modifiers |= os::kModCtrl;
                name = After(name, "ctrl+");
            }
            else if (StartsWithWord(name, "shortcut+"))  // the command shortcuts' modifier: Command on macOS
            {
                modifiers |= os::kModShortcut;
                name = After(name, "shortcut+");
            }
            else if (StartsWithWord(name, "shift+"))
            {
                modifiers |= os::kModShift;
                name = After(name, "shift+");
            }
            else if (StartsWithWord(name, "alt+"))
            {
                modifiers |= os::kModAlt;
                name = After(name, "alt+");
            }
            else
            {
                break;
            }
        }
        Key key = Key::Unknown;
        if (!ParseKey(name, &key))
        {
            return false;
        }
        Event e;
        e.type = EventType::KeyDown;
        e.key = key;
        e.modifiers = modifiers;
        surface->Post(e);
        e.type = EventType::KeyUp;
        surface->Post(e);
        return true;
    }
    if (StartsWithWord(word, "resize:"))
    {
        const StringView size = After(word, "resize:");
        const usize x = size.Find(StringView("x"));
        if (x >= size.Length() || host == nullptr)
        {
            return false;
        }
        const long w = CStrToLong(String<HeapAllocator>(HeapAllocator::Default(), size.Substring(0, x)).CStr());
        const long h = CStrToLong(String<HeapAllocator>(HeapAllocator::Default(), size.Substring(x + 1)).CStr());
        if (w <= 0 || h <= 0)
        {
            return false;
        }
        host->Resize(static_cast<u32>(w), static_cast<u32>(h));
        return true;
    }
    if (StartsWithWord(word, "theme:"))
    {
        const StringView which = After(word, "theme:");
        if (SameName(which, "dark"))
        {
            app->SetDark(true);
        }
        else if (SameName(which, "light"))
        {
            app->SetDark(false);
        }
        else
        {
            return false;
        }
        return true;
    }
    if (StartsWithWord(word, "menu:"))
    {
        const StringView path = After(word, "menu:");
        const usize slash = path.Find(StringView("/"));
        if (slash >= path.Length())
        {
            return false;
        }
        return app->ChooseMenuItem(path.Substring(0, slash), path.Substring(slash + 1));
    }
    if (StartsWithWord(word, "openmenu:"))
    {
        return app->OpenMenu(After(word, "openmenu:"));
    }
    if (StartsWithWord(word, "drag:"))
    {
        const StringView rest = After(word, "drag:");
        const usize colon = rest.Find(StringView(":"));
        if (colon >= rest.Length())
        {
            return false;
        }
        Widget* w = app->Find(rest.Substring(0, colon));
        if (w == nullptr)
        {
            return false;
        }
        const String<HeapAllocator> delta = OwnedWord(rest.Substring(colon + 1));
        char* end = nullptr;
        const f32 dx = static_cast<f32>(CStrToDouble(delta.CStr(), &end));
        if (end == nullptr || *end != ',')
        {
            return false;
        }
        const f32 dy = static_cast<f32>(CStrToDouble(end + 1, nullptr));
        const ui::Point from = CenterOf(w);
        PostPointer(surface, EventType::PointerMove, from, 0);
        PostPointer(surface, EventType::PointerDown, from, os::kMouseLeft);
        constexpr int kSteps = 8;
        for (int i = 1; i <= kSteps; ++i)
        {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(kSteps);
            PostPointer(surface, EventType::PointerMove, ui::Point{from.x + dx * t, from.y + dy * t}, 0);
        }
        PostPointer(surface, EventType::PointerUp, ui::Point{from.x + dx, from.y + dy}, os::kMouseLeft);
        return true;
    }
    if (StartsWithWord(word, "wheel:"))
    {
        const StringView rest = After(word, "wheel:");
        const usize colon = rest.Find(StringView(":"));
        if (colon >= rest.Length())
        {
            return false;
        }
        Widget* w = app->Find(rest.Substring(0, colon));
        if (w == nullptr)
        {
            return false;
        }
        Event e;
        e.type = EventType::Wheel;
        e.position = CenterOf(w);
        e.wheel_y = static_cast<f32>(CStrToDouble(OwnedWord(rest.Substring(colon + 1)).CStr()));
        surface->Post(e);
        return true;
    }
    if (StartsWithWord(word, "drop:text:") || StartsWithWord(word, "drop:file:"))
    {
        const bool is_text = word[5] == 't';
        Widget* target = app->Find(is_text ? StringView("text_name") : StringView("viewport"));
        if (target == nullptr)
        {
            return false;
        }
        const ui::Point at = CenterOf(target);
        DragPayload pending;
        pending.AddPending(is_text ? DragEntryKind::Text : DragEntryKind::FileList);
        const u8 allowed = os::DragActionBit(os::DragAction::Copy);
        surface->ExternalDragEnter(static_cast<DragPayload&&>(pending), at, allowed);
        surface->ExternalDragOver(at, allowed);
        DragPayload complete;
        const String<HeapAllocator> data = OwnedWord(After(word, is_text ? "drop:text:" : "drop:file:"));
        if (is_text)
        {
            complete.AddText(data.View());
        }
        else
        {
            complete.AddFiles(data.View());
        }
        surface->ExternalDrop(static_cast<DragPayload&&>(complete), at, os::DragAction::Copy);
        return true;
    }
    if (StartsWithWord(word, "paste:"))
    {
        surface->GetClipboard().SetText(After(word, "paste:"));
        Event e;
        e.type = EventType::KeyDown;
        e.key = Key::V;
        e.modifiers = os::kModShortcut;
        surface->Post(e);
        e.type = EventType::KeyUp;
        surface->Post(e);
        return true;
    }
    if (StartsWithWord(word, "shot:"))
    {
        if (host == nullptr)
        {
            return false;
        }
        host->Screenshot(After(word, "shot:"));
        return true;
    }
    return host != nullptr && host->Other(word);
}

}  // namespace ui_demo

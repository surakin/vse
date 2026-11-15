// Phase 23 Day 353: input the host forwards and requests the engine sends back, on a surface over a window the host
// owns. The host here is the test: it opens a window with the platform layer (X11, as a host with its own toolkit
// would), creates the engine and a surface, and forwards its events through `surface_input`. The engine's UI toolkit
// (ui::Surface with a button and text fields, bound to the surface's window by ui_window's WindowAdapter, the way an
// application binds it to its own window) reacts as it would to a window of its own: a click changes a counter that the
// test reads back through the world API, typed text reaches a field, the cursor and the clipboard requests leave
// through the host's callbacks.
#include <catch2/catch_test_macros.hpp>

#include "display_session.h"
#include "font_support.h"

#include "engine/engine_c.h"
#include "engine_c/surface.h"
#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "os/console.h"
#include "platform/foreign_window.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/text_field.h"
#include "ui_window/os_clipboard.h"
#include "ui_window/window_adapter.h"

using namespace engine_placeholder;

namespace
{

// What the host records of the engine's requests.
struct HostLog
{
    Vector<u32> cursors;
    char clipboard[64] = {};  // the host's clipboard
    usize clipboard_length = 0;
    char stored[64] = {};  // what the engine last stored
    usize stored_length = 0;
    int stores = 0;
    int text_rects = 0;
    i32 rect[4] = {0, 0, 0, 0};
    int focus_requests = 0;
    int focus_value = -1;
    int locks = 0;
};

int HostSetCursor(void* user, u32 shape)
{
    static_cast<HostLog*>(user)->cursors.Push(shape);
    return 1;
}

int HostSetLock(void* user, int locked)
{
    static_cast<HostLog*>(user)->locks += locked ? 1 : -1;
    return 1;
}

int HostClipboardSet(void* user, engine_string text)
{
    HostLog* log = static_cast<HostLog*>(user);
    if (text.length > sizeof(log->stored))
    {
        return 0;
    }
    MemCopy(log->stored, text.data, text.length);
    log->stored_length = text.length;
    MemCopy(log->clipboard, text.data, text.length);
    log->clipboard_length = text.length;
    ++log->stores;
    return 1;
}

int HostClipboardGet(void* user, engine_string* text)
{
    HostLog* log = static_cast<HostLog*>(user);
    if (log->clipboard_length == 0)
    {
        return 0;
    }
    text->data = log->clipboard;
    text->length = log->clipboard_length;
    return 1;
}

void HostTextRect(void* user, i32 x, i32 y, i32 w, i32 h)
{
    HostLog* log = static_cast<HostLog*>(user);
    log->rect[0] = x;
    log->rect[1] = y;
    log->rect[2] = w;
    log->rect[3] = h;
    ++log->text_rects;
}

void HostFocus(void* user, int focused)
{
    HostLog* log = static_cast<HostLog*>(user);
    ++log->focus_requests;
    log->focus_value = focused;
}

ui::Rect R(f32 x, f32 y, f32 w, f32 h)
{
    ui::Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// The counter the button changes lives in the engine's world: the click handler calls the API like any host code.
struct Counter
{
    const engine_api* api = nullptr;
    engine_handle world = {};
    u64 entity = 0;
};

void Clicked(void* user, ui::Button*)
{
    Counter* counter = static_cast<Counter*>(user);
    engine_transform t = {};
    t.struct_size = sizeof(t);
    REQUIRE(counter->api->component_get(counter->world, counter->entity, ENGINE_COMPONENT_TRANSFORM, &t) == ENGINE_OK);
    t.position[0] += 1.0f;
    REQUIRE(counter->api->component_set(counter->world, counter->entity, ENGINE_COMPONENT_TRANSFORM, &t) == ENGINE_OK);
}

struct Rig
{
    Rig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE)) {}

    ~Rig()
    {
        h.GetSurface().SetClipboard(nullptr);
        clip.Reset();  // before the surface's window goes
        if (surface.value != 0)
        {
            api->surface_destroy(surface);
        }
        if (world.value != 0)
        {
            api->world_destroy(world);
        }
        if (engine.value != 0)
        {
            api->destroy(engine);
        }
    }

    // Returns false (after a skip message) when this machine cannot run the test.
    bool Start()
    {
        REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) ==
                ENGINE_OK);
        engine_create_info create = {};
        create.struct_size = sizeof(create);
        create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
        REQUIRE(api->create(&create, &engine) == ENGINE_OK);
        if (!host.Open(os::WindowBackend::X11, "engine input test", 200, 120, &input))
        {
            return false;
        }
        for (int i = 0; i < 10; ++i)
        {
            host.Get()->PumpEvents();
        }
        const os::NativeWindowInfo native = host.Get()->GetNativeInfo();
        callbacks.struct_size = sizeof(callbacks);
        callbacks.user = &log;
        callbacks.set_cursor = &HostSetCursor;
        callbacks.set_pointer_lock = &HostSetLock;
        callbacks.clipboard_set = &HostClipboardSet;
        callbacks.clipboard_get = &HostClipboardGet;
        callbacks.text_input_rect = &HostTextRect;
        callbacks.request_focus = &HostFocus;
        engine_surface_info info = {};
        info.struct_size = sizeof(info);
        info.kind = ENGINE_SURFACE_X11;
        info.width = 200;
        info.height = 120;
        info.scale = 1.0f;
        info.display = native.display;
        info.window_id = native.window_id;
        info.callbacks = &callbacks;
        if (api->surface_create(engine, &info, &surface) != ENGINE_OK)
        {
            return false;
        }
        window = engine_c::SurfaceWindowOf(surface);
        REQUIRE(window != nullptr);
        REQUIRE(api->world_create(engine, &world) == ENGINE_OK);
        REQUIRE(api->entity_create(world, &counter.entity) == ENGINE_OK);
        engine_transform t = {};
        t.struct_size = sizeof(t);
        t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
        t.rotation[3] = 1.0f;
        REQUIRE(api->component_set(world, counter.entity, ENGINE_COMPONENT_TRANSFORM, &t) == ENGINE_OK);
        counter.api = api;
        counter.world = world;

        // The UI: a button and two fields, bound to the surface's window like to any window.
        clip = MakeUnique<ui::OsClipboard>(window);
        h.GetSurface().SetClipboard(clip.Get());
        adapter.SetClipboard(clip.Get());
        UniquePtr<ui::Button> b = MakeUnique<ui::Button>(StringView("go"), StringView("Go"));
        button = b.Get();
        b->SetBounds(R(10, 10, 80, 30));
        b->OnClick().Connect(&Clicked, &counter);
        h.GetSurface().Root()->AddChild(UniquePtr<ui::Widget>(b.Release()));
        UniquePtr<ui::TextField> f = MakeUnique<ui::TextField>(StringView("field"));
        field = f.Get();
        f->SetBounds(R(10, 60, 180, 28));
        h.GetSurface().Root()->AddChild(UniquePtr<ui::Widget>(f.Release()));
        h.Advance(0);
        h.Advance(0);
        return true;
    }

    engine_status Send(const engine_input_event& e) { return api->surface_input(surface, &e); }

    void Move(f64 x, f64 y)
    {
        engine_input_event e = {};
        e.struct_size = sizeof(e);
        e.type = ENGINE_INPUT_POINTER_MOVE;
        e.x = x;
        e.y = y;
        REQUIRE(Send(e) == ENGINE_OK);
    }
    void Button(bool down, f64 x, f64 y)
    {
        engine_input_event e = {};
        e.struct_size = sizeof(e);
        e.type = ENGINE_INPUT_POINTER_BUTTON;
        e.button = ENGINE_BUTTON_LEFT;
        e.down = down ? 1 : 0;
        e.x = x;
        e.y = y;
        REQUIRE(Send(e) == ENGINE_OK);
    }
    void Key(u32 key, bool down, u32 modifiers = 0)
    {
        engine_input_event e = {};
        e.struct_size = sizeof(e);
        e.type = ENGINE_INPUT_KEY;
        e.key = key;
        e.down = down ? 1 : 0;
        e.modifiers = modifiers;
        REQUIRE(Send(e) == ENGINE_OK);
    }
    void Type(const char* text)
    {
        engine_input_event e = {};
        e.struct_size = sizeof(e);
        e.type = ENGINE_INPUT_TEXT;
        e.text.data = text;
        e.text.length = CStrLength(text);
        REQUIRE(Send(e) == ENGINE_OK);
    }
    // The UI takes what the host forwarded (one pump of the host's loop).
    void Pump()
    {
        adapter.PumpInto(*window, h.GetSurface());
        h.Advance(0);
        adapter.UpdateCursor(*window, h.GetSurface());
    }
    f32 Clicks()
    {
        engine_transform t = {};
        t.struct_size = sizeof(t);
        REQUIRE(api->component_get(world, counter.entity, ENGINE_COMPONENT_TRANSFORM, &t) == ENGINE_OK);
        return t.position[0];
    }

    ui::Harness h;
    os::InputState input;
    os::PlatformWindowHost host;
    const engine_api* api = nullptr;
    engine_handle engine = {};
    engine_handle surface = {};
    engine_handle world = {};
    os::ForeignWindow* window = nullptr;
    HostLog log;
    engine_surface_callbacks callbacks = {};
    Counter counter;
    UniquePtr<ui::OsClipboard> clip;
    ui::WindowAdapter adapter;
    ui::Button* button = nullptr;
    ui::TextField* field = nullptr;
};

}  // namespace

TEST_CASE("Forwarded pointer events click a UI button and the click changes the engine's world",
          "[engine_c][surface][input][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    const char* display = os::GetEnvironment("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        SKIP("no X11 display");
    }
    Rig rig;
    if (!rig.Start())
    {
        SKIP("no window or no Vulkan device that can present to it");
    }
    REQUIRE(rig.Clicks() == 0.0f);

    rig.Move(30, 20);
    rig.Button(true, 30, 20);
    rig.Pump();
    CHECK(rig.Clicks() == 0.0f);  // a click is a release inside
    rig.Button(false, 30, 20);
    rig.Pump();
    CHECK(rig.Clicks() == 1.0f);

    // A release outside the button is no click.
    rig.Move(30, 20);
    rig.Button(true, 30, 20);
    rig.Move(150, 100);
    rig.Button(false, 150, 100);
    rig.Pump();
    CHECK(rig.Clicks() == 1.0f);

    // The first touch acts as the pointer with the left button; a second finger is ignored.
    engine_input_event touch = {};
    touch.struct_size = sizeof(touch);
    touch.type = ENGINE_INPUT_TOUCH;
    touch.touch_id = 7;
    touch.down = 1;
    touch.x = 40;
    touch.y = 25;
    REQUIRE(rig.Send(touch) == ENGINE_OK);
    engine_input_event second = touch;
    second.touch_id = 8;
    second.x = 150;
    second.y = 100;
    REQUIRE(rig.Send(second) == ENGINE_OK);
    touch.down = 0;
    REQUIRE(rig.Send(touch) == ENGINE_OK);
    rig.Pump();
    CHECK(rig.Clicks() == 2.0f);
}

TEST_CASE("Forwarded keys and text reach a UI text field, and the cursor request goes back to the host",
          "[engine_c][surface][input][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    const char* display = os::GetEnvironment("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        SKIP("no X11 display");
    }
    Rig rig;
    if (!rig.Start())
    {
        SKIP("no window or no Vulkan device that can present to it");
    }
    // The pointer enters the field: the UI asks for the text beam, and the request reaches the host's callback.
    rig.Move(60, 70);
    rig.Pump();
    REQUIRE(rig.log.cursors.Size() >= 1);
    CHECK(rig.log.cursors[rig.log.cursors.Size() - 1] == static_cast<u32>(ENGINE_CURSOR_I_BEAM));
    // And back to the arrow when it leaves.
    rig.Move(150, 110);
    rig.Pump();
    CHECK(rig.log.cursors[rig.log.cursors.Size() - 1] == static_cast<u32>(ENGINE_CURSOR_ARROW));

    // Click into the field and type, with a character outside ASCII and a backspace.
    rig.Move(60, 70);
    rig.Button(true, 60, 70);
    rig.Button(false, 60, 70);
    rig.Type("abc\xC3\xA9");
    rig.Key(ENGINE_KEY_BACKSPACE, true);
    rig.Key(ENGINE_KEY_BACKSPACE, false);
    rig.Type("d");
    rig.Pump();
    CHECK(rig.field->Text() == StringView("abcd"));

    // Ctrl+A, Ctrl+C: the engine puts the text on the host's clipboard through the callback.
    rig.Key(ENGINE_KEY_A, true, ENGINE_MOD_CTRL);
    rig.Key(ENGINE_KEY_A, false, ENGINE_MOD_CTRL);
    rig.Key(ENGINE_KEY_C, true, ENGINE_MOD_CTRL);
    rig.Key(ENGINE_KEY_C, false, ENGINE_MOD_CTRL);
    rig.Pump();
    CHECK(rig.log.stores == 1);
    CHECK(StringView(rig.log.stored, rig.log.stored_length) == StringView("abcd"));

    // The host's clipboard changes; Ctrl+V reads it through the callback and the text arrives as one paste.
    MemCopy(rig.log.clipboard, "pasted", 6);
    rig.log.clipboard_length = 6;
    rig.Key(ENGINE_KEY_V, true, ENGINE_MOD_CTRL);
    rig.Key(ENGINE_KEY_V, false, ENGINE_MOD_CTRL);
    rig.Pump();
    rig.Pump();
    CHECK(rig.field->Text() == StringView("pasted"));  // it replaced the selection of Ctrl+A

    // The text-input rectangle, the focus request and the pointer lock go back too.
    rig.window->SetTextInputRect(12, 34, 5, 20);
    CHECK(rig.log.text_rects == 1);
    CHECK(rig.log.rect[0] == 12);
    CHECK(rig.log.rect[1] == 34);
    CHECK(rig.log.rect[2] == 5);
    CHECK(rig.log.rect[3] == 20);
    rig.window->RequestFocus(true);
    CHECK(rig.log.focus_requests == 1);
    CHECK(rig.log.focus_value == 1);
    CHECK(rig.window->SetPointerLock(true));
    CHECK(rig.window->PointerLocked());
    CHECK(rig.window->SetPointerLock(false));
    CHECK_FALSE(rig.window->PointerLocked());
}

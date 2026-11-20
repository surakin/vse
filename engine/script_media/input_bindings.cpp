#include "script_media/input_bindings.h"

#include "script/partition_context.h"

namespace vse::script_media
{

using script::PartitionContext;
using script::ThreadRule;
using script::Value;

namespace
{

// The names of the keys, by `os::Key` number.
const char* const kKeyNames[] = {
    "unknown",   "a",          "b",          "c",         "d",         "e",           "f",
    "g",         "h",          "i",          "j",         "k",         "l",           "m",
    "n",         "o",          "p",          "q",         "r",         "s",           "t",
    "u",         "v",          "w",          "x",         "y",         "z",           "0",
    "1",         "2",          "3",          "4",         "5",         "6",           "7",
    "8",         "9",          "f1",         "f2",        "f3",        "f4",          "f5",
    "f6",        "f7",         "f8",         "f9",        "f10",       "f11",         "f12",
    "escape",    "enter",      "tab",        "backspace", "space",     "insert",      "delete",
    "home",      "end",        "pageup",     "pagedown",  "left",      "right",       "up",
    "down",      "leftshift",  "rightshift", "leftctrl",  "rightctrl", "leftalt",     "rightalt",
    "leftsuper", "rightsuper", "capslock",   "minus",     "equals",    "leftbracket", "rightbracket",
    "backslash", "semicolon",  "apostrophe", "grave",     "comma",     "period",      "slash"};
static_assert(sizeof(kKeyNames) / sizeof(kKeyNames[0]) == static_cast<usize>(os::Key::Count),
              "a name for every key, in the order of os::Key");

struct ButtonName
{
    const char* name;
    u32 bit;
};

const ButtonName kButtons[] = {{"left", os::kMouseLeft},
                               {"right", os::kMouseRight},
                               {"middle", os::kMouseMiddle},
                               {"back", os::kMouseBack},
                               {"forward", os::kMouseForward}};

PartitionContext* InputContext()
{
    PartitionContext* context = script::CurrentPartitionContext();
    if (context == nullptr)
    {
        script::RaiseScriptError("no world is available to this script");
        return nullptr;
    }
    return context;
}

const InputSnapshot* Snapshot()
{
    PartitionContext* context = InputContext();
    if (context == nullptr)
    {
        return nullptr;
    }
    const InputScriptServices* services = static_cast<const InputScriptServices*>(
        context->extensions[script::kServiceInput]);
    if (services == nullptr || services->snapshot == nullptr)
    {
        script::RaiseScriptError("this world has no input");
        return nullptr;
    }
    return services->snapshot;
}

void InputRaise(StringView a, StringView b, StringView c)
{
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(a);
    text.Append(b);
    text.Append(c);
    script::RaiseScriptError(text.View());
}

bool FindKey(StringView name, os::Key* key)
{
    for (usize i = 1; i < static_cast<usize>(os::Key::Count); ++i)
    {
        if (name == StringView(kKeyNames[i]))
        {
            *key = static_cast<os::Key>(i);
            return true;
        }
    }
    InputRaise("no key is called `", name,
               "` (lower case: a-z, 0-9, f1-f12, space, enter, escape, left, leftshift, ...)");
    return false;
}

bool FindButton(StringView name, u32* bit)
{
    for (const ButtonName& b : kButtons)
    {
        if (name == StringView(b.name))
        {
            *bit = b.bit;
            return true;
        }
    }
    InputRaise("no mouse button is called `", name, "` (left, right, middle, back, forward)");
    return false;
}

bool KeyDown(StringView name)
{
    const InputSnapshot* s = Snapshot();
    os::Key key;
    return s != nullptr && FindKey(name, &key) && s->KeyDown(key);
}

bool KeyPressed(StringView name)
{
    const InputSnapshot* s = Snapshot();
    os::Key key;
    return s != nullptr && FindKey(name, &key) && s->KeyPressed(key);
}

bool KeyReleased(StringView name)
{
    const InputSnapshot* s = Snapshot();
    os::Key key;
    return s != nullptr && FindKey(name, &key) && s->KeyReleased(key);
}

bool Modifier(StringView name)
{
    const InputSnapshot* s = Snapshot();
    if (s == nullptr)
    {
        return false;
    }
    u32 bit = 0;
    if (name == StringView("shift"))
    {
        bit = os::kModShift;
    }
    else if (name == StringView("ctrl"))
    {
        bit = os::kModCtrl;
    }
    else if (name == StringView("alt"))
    {
        bit = os::kModAlt;
    }
    else if (name == StringView("super"))
    {
        bit = os::kModSuper;
    }
    else
    {
        script::RaiseScriptError("the modifier is shift, ctrl, alt or super");
        return false;
    }
    return (s->Modifiers() & bit) != 0;
}

Vec2 MousePosition()
{
    const InputSnapshot* s = Snapshot();
    return s != nullptr ? Vec2(static_cast<f32>(s->MouseX()), static_cast<f32>(s->MouseY())) : Vec2(0.0f, 0.0f);
}

Vec2 MouseDelta()
{
    const InputSnapshot* s = Snapshot();
    return s != nullptr ? Vec2(static_cast<f32>(s->MouseDeltaX()), static_cast<f32>(s->MouseDeltaY()))
                        : Vec2(0.0f, 0.0f);
}

Vec2 Scroll()
{
    const InputSnapshot* s = Snapshot();
    return s != nullptr ? Vec2(static_cast<f32>(s->ScrollX()), static_cast<f32>(s->ScrollY())) : Vec2(0.0f, 0.0f);
}

bool MouseDown(StringView name)
{
    const InputSnapshot* s = Snapshot();
    u32 bit = 0;
    return s != nullptr && FindButton(name, &bit) && s->MouseDown(bit);
}

bool MousePressed(StringView name)
{
    const InputSnapshot* s = Snapshot();
    u32 bit = 0;
    return s != nullptr && FindButton(name, &bit) && s->MousePressed(bit);
}

bool MouseReleased(StringView name)
{
    const InputSnapshot* s = Snapshot();
    u32 bit = 0;
    return s != nullptr && FindButton(name, &bit) && s->MouseReleased(bit);
}

bool PointerInside()
{
    const InputSnapshot* s = Snapshot();
    return s != nullptr && s->PointerInside();
}

f64 Axis(StringView name)
{
    const InputSnapshot* s = Snapshot();
    f32 value = 0.0f;
    if (s != nullptr && !s->Axis(name, &value))
    {
        InputRaise("no input axis is called `", name, "`");
    }
    return static_cast<f64>(value);
}

bool ActionState(StringView name, int which)
{
    const InputSnapshot* s = Snapshot();
    if (s == nullptr)
    {
        return false;
    }
    script_media::ActionState state;
    if (!s->Action(name, &state))
    {
        InputRaise("no input action is called `", name, "`");
        return false;
    }
    return which == 0 ? state.down : which == 1 ? state.pressed : state.released;
}

bool Action(StringView name)
{
    return ActionState(name, 0);
}

bool ActionPressed(StringView name)
{
    return ActionState(name, 1);
}

bool ActionReleased(StringView name)
{
    return ActionState(name, 2);
}

StringView Text()
{
    const InputSnapshot* s = Snapshot();
    return s != nullptr ? s->Text() : StringView();
}

}  // namespace

bool RegisterInputBindings(script::BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&KeyDown>("input", "key_down", ThreadRule::WorldRead, "key",
                                  "Returns whether the key (a lower-case name such as \"w\", \"space\", \"leftshift\") "
                                  "was down at the start of the tick.");
    ok &= registry->Add<&KeyPressed>("input", "key_pressed", ThreadRule::WorldRead, "key",
                                     "Returns whether the key went down since the previous tick.");
    ok &= registry->Add<&KeyReleased>("input", "key_released", ThreadRule::WorldRead, "key",
                                      "Returns whether the key went up since the previous tick.");
    ok &= registry->Add<&Modifier>("input", "modifier", ThreadRule::WorldRead, "name",
                                   "Returns whether \"shift\", \"ctrl\", \"alt\" or \"super\" was held.");
    ok &= registry->Add<&MousePosition>("input", "mouse_position", ThreadRule::WorldRead, "",
                                        "Returns the mouse position in window pixels as a vec2.");
    ok &= registry->Add<&MouseDelta>("input", "mouse_delta", ThreadRule::WorldRead, "",
                                     "Returns the mouse movement since the previous tick.");
    ok &= registry->Add<&Scroll>("input", "scroll", ThreadRule::WorldRead, "",
                                 "Returns the scroll since the previous tick (10 units a notch).");
    ok &= registry->Add<&MouseDown>("input", "mouse_down", ThreadRule::WorldRead, "button",
                                    "Returns whether the mouse button (\"left\", \"right\", \"middle\", \"back\", "
                                    "\"forward\") was down.");
    ok &= registry->Add<&MousePressed>("input", "mouse_pressed", ThreadRule::WorldRead, "button",
                                       "Returns whether the button went down since the previous tick.");
    ok &= registry->Add<&MouseReleased>("input", "mouse_released", ThreadRule::WorldRead, "button",
                                        "Returns whether the button went up since the previous tick.");
    ok &= registry->Add<&PointerInside>("input", "pointer_inside", ThreadRule::WorldRead, "",
                                        "Returns whether the pointer is inside the window.");
    ok &= registry->Add<&Axis>("input", "axis", ThreadRule::WorldRead, "name",
                               "Returns the value of a named axis the host defined, from -1 to 1.");
    ok &= registry->Add<&Action>("input", "action", ThreadRule::WorldRead, "name",
                                 "Returns whether a named action the host defined is down.");
    ok &= registry->Add<&ActionPressed>("input", "action_pressed", ThreadRule::WorldRead, "name",
                                        "Returns whether the action went down since the previous tick.");
    ok &= registry->Add<&ActionReleased>("input", "action_released", ThreadRule::WorldRead, "name",
                                         "Returns whether the action went up since the previous tick.");
    ok &= registry->Add<&Text>("input", "text", ThreadRule::WorldRead, "",
                               "Returns the text typed since the previous tick (UTF-8).");
    return ok;
}

InputEventReport EmitInputEvents(const InputSnapshot& snapshot, script::ScriptRuntime* runtime)
{
    InputEventReport report;
    script::EventBus& bus = runtime->Events();
    auto emit = [&](Value payload)
    {
        if (bus.Emit(StringView(kEventInput), Move(payload), 0))
        {
            ++report.emitted;
        }
    };
    for (usize i = 1; i < static_cast<usize>(os::Key::Count); ++i)
    {
        const os::Key key = static_cast<os::Key>(i);
        const bool down = snapshot.KeyPressed(key);
        if (down || snapshot.KeyReleased(key))
        {
            Value payload = Value::MakeMap();
            payload.Set("kind", Value::FromText(StringView(down ? "key_down" : "key_up")));
            payload.Set("key", Value::FromText(StringView(kKeyNames[i])));
            emit(Move(payload));
        }
    }
    for (const ButtonName& b : kButtons)
    {
        const bool down = snapshot.MousePressed(b.bit);
        if (down || snapshot.MouseReleased(b.bit))
        {
            Value payload = Value::MakeMap();
            payload.Set("kind", Value::FromText(StringView(down ? "mouse_down" : "mouse_up")));
            payload.Set("button", Value::FromText(StringView(b.name)));
            emit(Move(payload));
        }
    }
    if (snapshot.ScrollX() != 0.0 || snapshot.ScrollY() != 0.0)
    {
        Value payload = Value::MakeMap();
        payload.Set("kind", Value::FromText(StringView("scroll")));
        payload.Set("dx", Value::FromFloat(snapshot.ScrollX()));
        payload.Set("dy", Value::FromFloat(snapshot.ScrollY()));
        emit(Move(payload));
    }
    if (snapshot.Text().Length() > 0)
    {
        Value payload = Value::MakeMap();
        payload.Set("kind", Value::FromText(StringView("text")));
        payload.Set("text", Value::FromText(snapshot.Text()));
        emit(Move(payload));
    }
    return report;
}

}  // namespace vse::script_media

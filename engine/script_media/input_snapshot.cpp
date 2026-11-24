#include "script_media/input_snapshot.h"

#include "algorithm.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace vse::script_media
{

namespace
{

constexpr u8 kSnapshotVersion = 1;

bool NameOk(StringView name)
{
    return name.Length() > 0 && name.Length() <= kMaxInputNameBytes;
}

bool BindingsOk(Span<const InputBinding> bindings)
{
    if (bindings.Size() > kMaxInputBindings)
    {
        return false;
    }
    for (usize i = 0; i < bindings.Size(); ++i)
    {
        const InputBinding& b = bindings[i];
        if (b.kind == InputBinding::kKey ? (b.key == os::Key::Unknown || b.key >= os::Key::Count) : b.buttons == 0)
        {
            return false;
        }
    }
    return true;
}

bool Down(const InputBinding& b, const os::KeyboardState& keys, u32 buttons)
{
    return b.kind == InputBinding::kKey ? keys.IsDown(b.key) : (buttons & b.buttons) != 0;
}

bool AnyDown(const Vector<InputBinding>& bindings, const os::KeyboardState& keys, u32 buttons)
{
    for (usize i = 0; i < bindings.Size(); ++i)
    {
        if (Down(bindings[i], keys, buttons))
        {
            return true;
        }
    }
    return false;
}

void Copy(Vector<InputBinding>* to, Span<const InputBinding> from)
{
    for (usize i = 0; i < from.Size(); ++i)
    {
        to->Push(from[i]);
    }
}

bool Finite(f64 v)
{
    return v > -1.0e15 && v < 1.0e15;  // NaN and the infinities fail; no f32 cast of a value out of its range
}

}  // namespace

bool InputMap::AddAction(StringView name, Span<const InputBinding> bindings)
{
    if (!NameOk(name) || bindings.Size() == 0 || !BindingsOk(bindings) || actions_.Size() >= kMaxInputNames)
    {
        return false;
    }
    for (usize i = 0; i < actions_.Size(); ++i)
    {
        if (actions_[i].name.View() == name)
        {
            return false;
        }
    }
    Action action;
    action.name.Append(name);
    Copy(&action.bindings, bindings);
    actions_.Push(Move(action));
    return true;
}

bool InputMap::AddAxis(StringView name, Span<const InputBinding> negative, Span<const InputBinding> positive)
{
    if (!NameOk(name) || (negative.Size() == 0 && positive.Size() == 0) || !BindingsOk(negative) ||
        !BindingsOk(positive) || axes_.Size() >= kMaxInputNames)
    {
        return false;
    }
    for (usize i = 0; i < axes_.Size(); ++i)
    {
        if (axes_[i].name.View() == name)
        {
            return false;
        }
    }
    Axis axis;
    axis.name.Append(name);
    Copy(&axis.negative, negative);
    Copy(&axis.positive, positive);
    axes_.Push(Move(axis));
    return true;
}

void InputSnapshot::Capture(u64 tick, const os::InputState& state, StringView text, const InputMap& map,
                            Span<const ExternalAxis> externals)
{
    before_ = keys_;
    buttons_before_ = buttons_;
    const f64 old_x = mouse_x_;
    const f64 old_y = mouse_y_;
    tick_ = tick;
    keys_ = state.keyboard;
    buttons_ = state.mouse_buttons;
    mouse_x_ = state.mouse_x;
    mouse_y_ = state.mouse_y;
    mouse_dx_ = captured_ ? state.mouse_x - old_x : 0.0;
    mouse_dy_ = captured_ ? state.mouse_y - old_y : 0.0;
    scroll_x_ = state.scroll_dx;
    scroll_y_ = state.scroll_dy;
    inside_ = state.pointer_inside;
    captured_ = true;
    text_ = String<HeapAllocator>(HeapAllocator::Default());
    usize keep = text.Length() > kMaxInputTextBytes ? kMaxInputTextBytes : text.Length();
    while (keep > 0 && keep < text.Length() && (static_cast<u8>(text[keep]) & 0xC0) == 0x80)
    {
        --keep;  // never cut a UTF-8 sequence in the middle
    }
    text_.Append(StringView(text.Data(), keep));

    actions_.Clear();
    for (usize i = 0; i < map.Actions().Size(); ++i)
    {
        const InputMap::Action& action = map.Actions()[i];
        NamedAction named;
        named.name.Append(action.name.View());
        const bool now = AnyDown(action.bindings, keys_, buttons_);
        const bool was = AnyDown(action.bindings, before_, buttons_before_);
        named.state.down = now;
        named.state.pressed = now && !was;
        named.state.released = !now && was;
        actions_.Push(Move(named));
    }
    axes_.Clear();
    for (usize i = 0; i < map.Axes().Size(); ++i)
    {
        const InputMap::Axis& axis = map.Axes()[i];
        NamedAxis named;
        named.name.Append(axis.name.View());
        f32 value = 0.0f;
        value += AnyDown(axis.positive, keys_, buttons_) ? 1.0f : 0.0f;
        value -= AnyDown(axis.negative, keys_, buttons_) ? 1.0f : 0.0f;
        for (usize e = 0; e < externals.Size(); ++e)
        {
            if (externals[e].name == axis.name.View() && IsFinite(externals[e].value))
            {
                value += externals[e].value;
            }
        }
        named.value = Clamp(value, -1.0f, 1.0f);
        axes_.Push(Move(named));
    }
    for (usize e = 0; e < externals.Size(); ++e)  // a value for a name the map does not have is an axis of its own
    {
        bool known = false;
        for (usize i = 0; i < axes_.Size(); ++i)
        {
            known = known || axes_[i].name.View() == externals[e].name;
        }
        if (!known && NameOk(externals[e].name) && axes_.Size() < kMaxInputNames && IsFinite(externals[e].value))
        {
            NamedAxis named;
            named.name.Append(externals[e].name);
            named.value = Clamp(externals[e].value, -1.0f, 1.0f);
            axes_.Push(Move(named));
        }
    }
}

bool InputSnapshot::Action(StringView name, ActionState* out) const
{
    *out = ActionState();
    for (usize i = 0; i < actions_.Size(); ++i)
    {
        if (actions_[i].name.View() == name)
        {
            *out = actions_[i].state;
            return true;
        }
    }
    return false;
}

bool InputSnapshot::Axis(StringView name, f32* out) const
{
    *out = 0.0f;
    for (usize i = 0; i < axes_.Size(); ++i)
    {
        if (axes_[i].name.View() == name)
        {
            *out = axes_[i].value;
            return true;
        }
    }
    return false;
}

namespace
{

void WriteKeys(bytes::ByteWriter* out, const os::KeyboardState& keys)
{
    u64 bits[2] = {0, 0};
    for (usize k = 1; k < os::KeyboardState::kKeyCount; ++k)
    {
        if (keys.IsDown(static_cast<os::Key>(k)))
        {
            bits[k / 64] |= u64{1} << (k % 64);
        }
    }
    out->WriteU64(bits[0]);
    out->WriteU64(bits[1]);
    out->WriteU32(keys.modifiers);
}

bool ReadKeys(bytes::ByteReader* in, os::KeyboardState* keys)
{
    u64 bits[2] = {0, 0};
    u32 modifiers = 0;
    if (in->ReadU64(&bits[0]) != bytes::ByteError::Ok || in->ReadU64(&bits[1]) != bytes::ByteError::Ok ||
        in->ReadU32(&modifiers) != bytes::ByteError::Ok)
    {
        return false;
    }
    *keys = os::KeyboardState();
    for (usize k = 1; k < os::KeyboardState::kKeyCount; ++k)
    {
        keys->SetDown(static_cast<os::Key>(k), (bits[k / 64] >> (k % 64) & 1u) != 0);
    }
    keys->modifiers = modifiers;
    return true;
}

void WriteName(bytes::ByteWriter* out, StringView name)
{
    out->WriteU8(static_cast<u8>(name.Length()));
    out->WriteBytes(ByteSpan(reinterpret_cast<const byte*>(name.Data()), name.Length()));
}

bool ReadName(bytes::ByteReader* in, String<HeapAllocator>* name)
{
    u8 size = 0;
    ByteSpan bytes;
    if (in->ReadU8(&size) != bytes::ByteError::Ok || size == 0 || size > kMaxInputNameBytes ||
        in->ReadBytes(size, &bytes) != bytes::ByteError::Ok)
    {
        return false;
    }
    *name = String<HeapAllocator>(HeapAllocator::Default());
    name->Append(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
    return true;
}

}  // namespace

void InputSnapshot::Serialize(bytes::ByteWriter* out) const
{
    out->WriteU8(kSnapshotVersion);
    out->WriteU64(tick_);
    WriteKeys(out, keys_);
    WriteKeys(out, before_);
    out->WriteU32(buttons_);
    out->WriteU32(buttons_before_);
    out->WriteF64(mouse_x_);
    out->WriteF64(mouse_y_);
    out->WriteF64(mouse_dx_);
    out->WriteF64(mouse_dy_);
    out->WriteF64(scroll_x_);
    out->WriteF64(scroll_y_);
    out->WriteBool(inside_);
    out->WriteBool(captured_);
    out->WriteU32(static_cast<u32>(text_.Length()));
    out->WriteBytes(ByteSpan(reinterpret_cast<const byte*>(text_.CStr()), text_.Length()));
    out->WriteU32(static_cast<u32>(actions_.Size()));
    for (usize i = 0; i < actions_.Size(); ++i)
    {
        WriteName(out, actions_[i].name.View());
        out->WriteU8(static_cast<u8>((actions_[i].state.down ? 1 : 0) | (actions_[i].state.pressed ? 2 : 0) |
                                     (actions_[i].state.released ? 4 : 0)));
    }
    out->WriteU32(static_cast<u32>(axes_.Size()));
    for (usize i = 0; i < axes_.Size(); ++i)
    {
        WriteName(out, axes_[i].name.View());
        out->WriteF32(axes_[i].value);
    }
}

bool InputSnapshot::Deserialize(bytes::ByteReader* in)
{
    InputSnapshot fresh;
    u8 version = 0;
    u32 text_size = 0;
    ByteSpan text;
    u32 count = 0;
    bool ok = in->ReadU8(&version) == bytes::ByteError::Ok && version == kSnapshotVersion &&
              in->ReadU64(&fresh.tick_) == bytes::ByteError::Ok && ReadKeys(in, &fresh.keys_) &&
              ReadKeys(in, &fresh.before_) && in->ReadU32(&fresh.buttons_) == bytes::ByteError::Ok &&
              in->ReadU32(&fresh.buttons_before_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.mouse_x_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.mouse_y_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.mouse_dx_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.mouse_dy_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.scroll_x_) == bytes::ByteError::Ok &&
              in->ReadF64(&fresh.scroll_y_) == bytes::ByteError::Ok &&
              in->ReadBool(&fresh.inside_) == bytes::ByteError::Ok &&
              in->ReadBool(&fresh.captured_) == bytes::ByteError::Ok &&
              in->ReadU32(&text_size) == bytes::ByteError::Ok && text_size <= kMaxInputTextBytes &&
              in->ReadBytes(text_size, &text) == bytes::ByteError::Ok && in->ReadU32(&count) == bytes::ByteError::Ok &&
              count <= kMaxInputNames;
    ok = ok && Finite(fresh.mouse_x_) && Finite(fresh.mouse_y_) && Finite(fresh.mouse_dx_) && Finite(fresh.mouse_dy_) &&
         Finite(fresh.scroll_x_) && Finite(fresh.scroll_y_);
    if (ok)
    {
        fresh.text_.Append(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()));
    }
    for (u32 i = 0; ok && i < count; ++i)
    {
        NamedAction action;
        u8 flags = 0;
        ok = ReadName(in, &action.name) && in->ReadU8(&flags) == bytes::ByteError::Ok && flags <= 7;
        if (ok)
        {
            action.state.down = (flags & 1) != 0;
            action.state.pressed = (flags & 2) != 0;
            action.state.released = (flags & 4) != 0;
            fresh.actions_.Push(Move(action));
        }
    }
    ok = ok && in->ReadU32(&count) == bytes::ByteError::Ok && count <= kMaxInputNames;
    for (u32 i = 0; ok && i < count; ++i)
    {
        NamedAxis axis;
        ok = ReadName(in, &axis.name) && in->ReadF32(&axis.value) == bytes::ByteError::Ok && IsFinite(axis.value);
        if (ok)
        {
            fresh.axes_.Push(Move(axis));
        }
    }
    if (!ok)
    {
        *this = InputSnapshot();
        return false;
    }
    *this = Move(fresh);
    return true;
}

bool InputSnapshot::Equals(const InputSnapshot& other) const
{
    if (tick_ != other.tick_ || buttons_ != other.buttons_ || buttons_before_ != other.buttons_before_ ||
        mouse_x_ != other.mouse_x_ || mouse_y_ != other.mouse_y_ || mouse_dx_ != other.mouse_dx_ ||
        mouse_dy_ != other.mouse_dy_ || scroll_x_ != other.scroll_x_ || scroll_y_ != other.scroll_y_ ||
        inside_ != other.inside_ || keys_.modifiers != other.keys_.modifiers || !(text_.View() == other.text_.View()) ||
        actions_.Size() != other.actions_.Size() || axes_.Size() != other.axes_.Size())
    {
        return false;
    }
    for (usize k = 1; k < os::KeyboardState::kKeyCount; ++k)
    {
        const os::Key key = static_cast<os::Key>(k);
        if (keys_.IsDown(key) != other.keys_.IsDown(key) || before_.IsDown(key) != other.before_.IsDown(key))
        {
            return false;
        }
    }
    for (usize i = 0; i < actions_.Size(); ++i)
    {
        const ActionState& a = actions_[i].state;
        const ActionState& b = other.actions_[i].state;
        if (!(actions_[i].name.View() == other.actions_[i].name.View()) || a.down != b.down || a.pressed != b.pressed ||
            a.released != b.released)
        {
            return false;
        }
    }
    for (usize i = 0; i < axes_.Size(); ++i)
    {
        if (!(axes_[i].name.View() == other.axes_[i].name.View()) || axes_[i].value != other.axes_[i].value)
        {
            return false;
        }
    }
    return true;
}

}  // namespace vse::script_media

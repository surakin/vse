#include "script_physics/physics_layers.h"

#include "settings/setting.h"

namespace vse::script_physics
{

namespace
{

void Fail(String<HeapAllocator>* error, StringView a, StringView b = StringView())
{
    if (error != nullptr)
    {
        *error = String<HeapAllocator>(HeapAllocator::Default());
        error->Append(a);
        error->Append(b);
    }
}

bool IsNameChar(char c, bool first)
{
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    return letter || (!first && c >= '0' && c <= '9');
}

}  // namespace

bool PhysicsLayers::Parse(StringView text, String<HeapAllocator>* error)
{
    Vector<String<HeapAllocator>> names;
    usize start = 0;
    const usize length = text.Length();
    while (start <= length)
    {
        usize end = start;
        while (end < length && text[end] != ',')
        {
            ++end;
        }
        usize first = start;
        usize last = end;
        while (first < last && (text[first] == ' ' || text[first] == '\t'))
        {
            ++first;
        }
        while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t'))
        {
            --last;
        }
        const StringView name(text.Data() + first, last - first);
        if (name.Empty())
        {
            if (length != 0)
            {
                Fail(error, "a layer name is empty");
                return false;
            }
        }
        else
        {
            for (usize i = 0; i < name.Length(); ++i)
            {
                if (!IsNameChar(name[i], i == 0))
                {
                    Fail(error, "a layer name has letters, digits and _ only: ", name);
                    return false;
                }
            }
            for (usize i = 0; i < names.Size(); ++i)
            {
                if (names[i].View() == name)
                {
                    Fail(error, "a layer name is used twice: ", name);
                    return false;
                }
            }
            if (names.Size() == kMaxPhysicsLayers)
            {
                Fail(error, "there are more than 32 layers");
                return false;
            }
            names.Push(String<HeapAllocator>(HeapAllocator::Default(), name));
        }
        start = end + 1;
    }
    if (names.Empty())
    {
        names.Push(String<HeapAllocator>(HeapAllocator::Default(), "default"));
    }
    names_ = Move(names);
    return true;
}

bool PhysicsLayers::Find(StringView name, u32* index) const
{
    for (usize i = 0; i < names_.Size(); ++i)
    {
        if (names_[i].View() == name)
        {
            *index = static_cast<u32>(i);
            return true;
        }
    }
    return false;
}

bool PhysicsLayers::MaskOf(const script::Value& selection, u32* mask, String<HeapAllocator>* error) const
{
    if (selection.IsNil())
    {
        *mask = 0xFFFFFFFFu;
        return true;
    }
    const auto one = [&](const script::Value& v, u32* bits)
    {
        u32 layer = 0;
        if (v.Kind() != script::ValueKind::Text)
        {
            Fail(error, "a layer is named by text");
            return false;
        }
        if (!Find(v.Text(), &layer))
        {
            Fail(error, "there is no physics layer named ", v.Text());
            return false;
        }
        *bits |= u32{1} << layer;
        return true;
    };
    u32 bits = 0;
    if (selection.Kind() == script::ValueKind::Text)
    {
        if (!one(selection, &bits))
        {
            return false;
        }
    }
    else if (selection.Kind() == script::ValueKind::Array)
    {
        for (usize i = 0; i < selection.Size(); ++i)
        {
            if (!one(selection.At(i), &bits))
            {
                return false;
            }
        }
    }
    else
    {
        Fail(error, "the layers are nil (all), a layer name or an array of names");
        return false;
    }
    *mask = bits;
    return true;
}

void RegisterPhysicsScriptSettings(settings::Registry* registry)
{
    settings::SettingDesc layers = settings::MakeText("script.physics_layers", "default");
    layers.env = "ENGINE_SCRIPT_PHYSICS_LAYERS";
    layers.flag = "script-physics-layers";
    layers.label = "Physics layer names";
    layers.description =
        "the names scripts give physics layers, separated by commas; a name's position is its layer number (0 to 31): "
        "default,world,player,enemy";
    layers.group = "script";
    layers.flags = settings::kRestartRequired;
    registry->Register(layers);
}

bool PhysicsLayersFromSettings(const settings::Registry& registry, PhysicsLayers* layers)
{
    const settings::SettingId id = registry.Find(StringView("script.physics_layers"));
    if (!id.IsValid())
    {
        return layers->Parse(StringView("default"), nullptr);
    }
    char text[1024];
    const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
    if (length < sizeof(text) && layers->Parse(StringView(text, length), nullptr))
    {
        return true;
    }
    layers->Parse(StringView("default"), nullptr);
    return false;
}

}  // namespace vse::script_physics

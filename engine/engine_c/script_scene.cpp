#include "engine_c/script_scene.h"

#include "engine_c/runtime.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/scalar.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/service_slots.h"
#include "script/value.h"

namespace vse::engine_c
{

using script::CommandKind;
using script::CommandKindTable;
using script::MergeContext;
using script::PartitionContext;
using script::ThreadRule;
using script::Value;
using script::ValueKind;

namespace
{

// The bits of the payloads' masks: which fields the script named.
constexpr u32 kEnvAmbientColor = 1u << 0;
constexpr u32 kEnvAmbientIntensity = 1u << 1;
constexpr u32 kEnvSkyColor = 1u << 2;
constexpr u32 kEnvExposure = 1u << 3;
constexpr u32 kLightColor = 1u << 0;
constexpr u32 kLightIntensity = 1u << 1;
constexpr u32 kLightRadius = 1u << 2;
constexpr u32 kLightInnerCone = 1u << 3;
constexpr u32 kLightOuterCone = 1u << 4;

constexpr f32 kMaxSpotCone = 1.5707964f;  // pi / 2, as the C API's spot light

// Plain floats and integers only, so a payload has no padding.
struct EnvironmentPayload
{
    u32 mask;
    f32 ambient_color[3];
    f32 ambient_intensity;
    f32 sky_color[3];
    f32 exposure;
};

struct LightPayload
{
    u32 kind;  // ENGINE_LIGHT_*; the kind of a created light (0 for `light.set`)
    u32 mask;
    f32 color[3];
    f32 intensity;
    f32 radius;
    f32 inner_cone;
    f32 outer_cone;
};

void SceneRaise(StringView a, StringView b = StringView(), StringView c = StringView(), StringView d = StringView(),
                StringView e = StringView())
{
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(a);
    text.Append(b);
    text.Append(c);
    text.Append(d);
    text.Append(e);
    script::RaiseScriptError(text.View());
}

PartitionContext* SceneContext()
{
    PartitionContext* context = script::CurrentPartitionContext();
    if (context == nullptr || context->world == nullptr)
    {
        script::RaiseScriptError("no world is available to this script");
        return nullptr;
    }
    return context;
}

const SceneScriptServices* SceneServicesOf(PartitionContext* context)
{
    const SceneScriptServices* services = static_cast<const SceneScriptServices*>(
        context->extensions[script::kServiceScene]);
    if (services == nullptr || services->object == nullptr)
    {
        script::RaiseScriptError("this world has no scene to change");
        return nullptr;
    }
    return services;
}

bool SceneRecord(PartitionContext* context, u16 kind, ecs::Entity target, const void* payload, u32 size)
{
    if (context->commands == nullptr)
    {
        script::RaiseScriptError("this script cannot change the world (no command buffer)");
        return false;
    }
    if (!context->commands->Record(kind, context->current_entity_key, target, payload, size))
    {
        script::RaiseScriptError("the command buffer is full");
        return false;
    }
    return true;
}

bool SceneFinite(f32 v)
{
    return IsFinite(v);
}

// ------------------------------------------------------------------ reading the table

// Raises for a key of `fields` that is not in `allowed` (a null-terminated list of names).
bool SceneKnownKeys(const Value& fields, StringView what, const char* const* allowed)
{
    for (usize i = 0; i < fields.Size(); ++i)
    {
        const StringView key = fields.KeyAt(i);
        bool known = false;
        for (usize a = 0; allowed[a] != nullptr; ++a)
        {
            known = known || key == StringView(allowed[a]);
        }
        if (!known)
        {
            SceneRaise(StringView("the "), what, StringView(" has no field `"), key, StringView("`"));
            return false;
        }
    }
    return true;
}

// Reads `key` as a number when present: `*mask` gets `bit`; raises on a value that is not a finite number.
bool SceneReadNumber(const Value& fields, StringView what, StringView key, u32 bit, f32* out, u32* mask)
{
    const Value* v = fields.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (!v->IsNumber() || !SceneFinite(static_cast<f32>(v->ToDouble())))
    {
        SceneRaise(StringView("the "), what, StringView("'s `"), key, StringView("` must be a finite number"));
        return false;
    }
    *out = static_cast<f32>(v->ToDouble());
    *mask |= bit;
    return true;
}

bool SceneReadVec3(const Value& fields, StringView what, StringView key, u32 bit, f32 out[3], u32* mask)
{
    const Value* v = fields.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (v->Kind() != ValueKind::Vec3)
    {
        SceneRaise(StringView("the "), what, StringView("'s `"), key, StringView("` must be a vec3"));
        return false;
    }
    const Vec3 value = v->AsVec3();
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
    *mask |= bit;
    return true;
}

bool SceneColorValid(const f32 c[3])
{
    return SceneFinite(c[0]) && SceneFinite(c[1]) && SceneFinite(c[2]) && c[0] >= 0.0f && c[1] >= 0.0f && c[2] >= 0.0f;
}

// ------------------------------------------------------------------ the environment

// Raises when a named field is outside its range.
bool EnvironmentValid(const EnvironmentPayload& p)
{
    if ((p.mask & kEnvAmbientColor) != 0 && !SceneColorValid(p.ambient_color))
    {
        script::RaiseScriptError("the environment's `ambient_color` must be finite and at least 0");
        return false;
    }
    if ((p.mask & kEnvSkyColor) != 0 && !SceneColorValid(p.sky_color))
    {
        script::RaiseScriptError("the environment's `sky_color` must be finite and at least 0");
        return false;
    }
    if ((p.mask & kEnvAmbientIntensity) != 0 && !(SceneFinite(p.ambient_intensity) && p.ambient_intensity >= 0.0f))
    {
        script::RaiseScriptError("the environment's `ambient_intensity` must be finite and at least 0");
        return false;
    }
    if ((p.mask & kEnvExposure) != 0 && !(SceneFinite(p.exposure) && p.exposure > 0.0f))
    {
        script::RaiseScriptError("the environment's `exposure` must be finite and above 0");
        return false;
    }
    return true;
}

void EnvironmentSet(Value fields)
{
    PartitionContext* context = SceneContext();
    if (context == nullptr || SceneServicesOf(context) == nullptr)
    {
        return;
    }
    if (fields.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("environment.set takes a table of fields");
        return;
    }
    static const char* const kAllowed[] = {"ambient_color", "ambient_intensity", "sky_color", "exposure", nullptr};
    if (!SceneKnownKeys(fields, StringView("environment"), kAllowed))
    {
        return;
    }
    EnvironmentPayload p{};
    if (!SceneReadVec3(fields, StringView("environment"), StringView("ambient_color"), kEnvAmbientColor,
                       p.ambient_color, &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("ambient_intensity"), kEnvAmbientIntensity,
                         &p.ambient_intensity, &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sky_color"), kEnvSkyColor, p.sky_color,
                       &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("exposure"), kEnvExposure, &p.exposure,
                         &p.mask) ||
        !EnvironmentValid(p))
    {
        return;
    }
    if (p.mask != 0)
    {
        SceneRecord(context, kCommandEnvironmentSet, ecs::Entity(), &p, sizeof(p));
    }
}

Value EnvironmentGet()
{
    PartitionContext* context = SceneContext();
    const SceneScriptServices* services = context != nullptr ? SceneServicesOf(context) : nullptr;
    if (services == nullptr)
    {
        return Value();
    }
    // The scripts run against the tick-start state: the environment is changed only by the merge and by the host
    // between frames, never while the scripts run.
    const Environment& e = services->object->environment;
    Value map = Value::MakeMap();
    map.Set("ambient_color", Value::FromVec3(Vec3(e.ambient_color[0], e.ambient_color[1], e.ambient_color[2])));
    map.Set("ambient_intensity", Value::FromFloat(e.ambient_intensity));
    map.Set("sky_color", Value::FromVec3(Vec3(e.sky_color[0], e.sky_color[1], e.sky_color[2])));
    map.Set("exposure", Value::FromFloat(e.exposure));
    return map;
}

bool ApplyEnvironmentSet(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const script::MergeServices* merge = static_cast<const script::MergeServices*>(context.User());
    const SceneScriptServices* services = merge != nullptr ? static_cast<const SceneScriptServices*>(
                                                                 merge->extensions[script::kServiceScene])
                                                           : nullptr;
    EnvironmentPayload p;
    if (services == nullptr || services->object == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    Environment& e = services->object->environment;
    if ((p.mask & kEnvAmbientColor) != 0)
    {
        MemCopy(e.ambient_color, p.ambient_color, sizeof(e.ambient_color));
    }
    if ((p.mask & kEnvAmbientIntensity) != 0)
    {
        e.ambient_intensity = p.ambient_intensity;
    }
    if ((p.mask & kEnvSkyColor) != 0)
    {
        MemCopy(e.sky_color, p.sky_color, sizeof(e.sky_color));
    }
    if ((p.mask & kEnvExposure) != 0)
    {
        e.exposure = p.exposure;
    }
    services->object->environment_set = true;  // as `world_set_environment`: the default sun goes
    return true;
}

// ------------------------------------------------------------------ lights

// Whether a light is valid: the same rules as the C API's `engine_light`. `why` names the first failure.
bool LightValid(const LightComponent& l, const char** why)
{
    if (!SceneColorValid(l.color))
    {
        *why = "the light's `color` must be finite and at least 0";
        return false;
    }
    if (!(SceneFinite(l.intensity) && l.intensity >= 0.0f))
    {
        *why = "the light's `intensity` must be finite and at least 0";
        return false;
    }
    if (l.kind != ENGINE_LIGHT_DIRECTIONAL && !(SceneFinite(l.radius) && l.radius > 0.0f))
    {
        *why = "the light's `radius` must be finite and above 0";
        return false;
    }
    if (l.kind == ENGINE_LIGHT_SPOT &&
        !(SceneFinite(l.inner_cone) && SceneFinite(l.outer_cone) && l.inner_cone >= 0.0f &&
          l.inner_cone < l.outer_cone && l.outer_cone <= kMaxSpotCone))
    {
        *why = "a spot light needs 0 <= inner_cone < outer_cone <= pi/2";
        return false;
    }
    return true;
}

// Puts the fields the mask names into `light`.
void LightFill(LightComponent* light, const LightPayload& p)
{
    if ((p.mask & kLightColor) != 0)
    {
        MemCopy(light->color, p.color, sizeof(light->color));
    }
    if ((p.mask & kLightIntensity) != 0)
    {
        light->intensity = p.intensity;
    }
    if ((p.mask & kLightRadius) != 0)
    {
        light->radius = p.radius;
    }
    if ((p.mask & kLightInnerCone) != 0)
    {
        light->inner_cone = p.inner_cone;
    }
    if ((p.mask & kLightOuterCone) != 0)
    {
        light->outer_cone = p.outer_cone;
    }
}

// Reads the table of a light call into `p` (and validates field by field against `light`, the light the fields will
// change); raises on any error.
bool LightRead(const Value& fields, LightComponent light, LightPayload* p)
{
    static const char* const kAllowed[] = {"color", "intensity", "radius", "inner_cone", "outer_cone", nullptr};
    if (fields.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("the light's fields are a table");
        return false;
    }
    if (!SceneKnownKeys(fields, StringView("light"), kAllowed) ||
        !SceneReadVec3(fields, StringView("light"), StringView("color"), kLightColor, p->color, &p->mask) ||
        !SceneReadNumber(fields, StringView("light"), StringView("intensity"), kLightIntensity, &p->intensity,
                         &p->mask) ||
        !SceneReadNumber(fields, StringView("light"), StringView("radius"), kLightRadius, &p->radius, &p->mask) ||
        !SceneReadNumber(fields, StringView("light"), StringView("inner_cone"), kLightInnerCone, &p->inner_cone,
                         &p->mask) ||
        !SceneReadNumber(fields, StringView("light"), StringView("outer_cone"), kLightOuterCone, &p->outer_cone,
                         &p->mask))
    {
        return false;
    }
    LightFill(&light, *p);
    const char* why = nullptr;
    if (!LightValid(light, &why))
    {
        script::RaiseScriptError(StringView(why));
        return false;
    }
    return true;
}

void LightSet(ecs::Entity e, Value fields)
{
    PartitionContext* context = SceneContext();
    if (context == nullptr || SceneServicesOf(context) == nullptr)
    {
        return;
    }
    // An entity spawned in this tick (a placeholder) takes `light.create`, not a change: it has no light yet.
    const LightComponent* current = e.IsPlaceholder() ? nullptr : context->world->Get<LightComponent>(e);
    if (current == nullptr)
    {
        script::RaiseScriptError("the entity has no light (`light.create` makes one)");
        return;
    }
    LightPayload p{};
    if (LightRead(fields, *current, &p) && p.mask != 0)
    {
        SceneRecord(context, kCommandLightSet, e, &p, sizeof(p));
    }
}

void LightCreate(ecs::Entity e, StringView kind, Value fields)
{
    PartitionContext* context = SceneContext();
    if (context == nullptr || SceneServicesOf(context) == nullptr)
    {
        return;
    }
    LightComponent light;
    if (kind == StringView("directional"))
    {
        light.kind = ENGINE_LIGHT_DIRECTIONAL;
        light.radius = 1.0f;  // ignored for a directional light, as in the C API
    }
    else if (kind == StringView("point"))
    {
        light.kind = ENGINE_LIGHT_POINT;
    }
    else if (kind == StringView("spot"))
    {
        light.kind = ENGINE_LIGHT_SPOT;
    }
    else
    {
        script::RaiseScriptError("the light kind is `directional`, `point` or `spot`");
        return;
    }
    if (!e.IsPlaceholder())
    {
        if (context->world->Has<LightComponent>(e))
        {
            script::RaiseScriptError("the entity already has a light (`light.set` changes it)");
            return;
        }
        if (!context->world->Has<ecs::LocalTransform>(e))
        {
            script::RaiseScriptError("the entity needs a transform to hold a light");
            return;
        }
    }
    LightPayload p{};
    p.kind = light.kind;
    if (LightRead(fields.IsNil() ? Value::MakeMap() : fields, light, &p))
    {
        SceneRecord(context, kCommandLightCreate, e, &p, sizeof(p));
    }
}

bool ApplyLightSet(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    LightPayload p;
    const LightComponent* current = context.World()->Get<LightComponent>(target);
    if (current == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    LightComponent light = *current;
    LightFill(&light, p);
    const char* why = nullptr;
    return LightValid(light, &why) && context.World()->Set(target, light) == ecs::Status::Ok;
}

bool ApplyLightCreate(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    LightPayload p;
    if (payload.Size() != sizeof(p) || !context.World()->Exists(target) || context.World()->Has<LightComponent>(target))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    LightComponent light;
    light.kind = p.kind;
    if (p.kind == ENGINE_LIGHT_DIRECTIONAL)
    {
        light.radius = 1.0f;
    }
    LightFill(&light, p);
    const char* why = nullptr;
    return LightValid(light, &why) && context.World()->Add(target, light) == ecs::Status::Ok;
}

}  // namespace

bool RegisterSceneCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandEnvironmentSet,
                          CommandKind{"environment.set", &ApplyEnvironmentSet, false, false, true});
    ok &= kinds->Register(kCommandLightSet, CommandKind{"light.set", &ApplyLightSet, false, true});
    ok &= kinds->Register(kCommandLightCreate, CommandKind{"light.create", &ApplyLightCreate, false, true});
    return ok;
}

bool RegisterSceneBindings(script::BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&EnvironmentSet>(
        "environment", "set", ThreadRule::WorldWrite, "fields",
        "Changes the world's environment at the sync point; every surface that shows the world uses it from its next "
        "frame. `fields` is a table with any of `ambient_color` (a vec3, linear RGB, at least 0), `ambient_intensity` "
        "(at least 0), `sky_color` (a vec3, at least 0) and `exposure` (above 0); a field left out keeps its value, "
        "and calling it every tick is fine. An unknown field or a value out of range is an error naming it. As "
        "`world_set_environment` in the C API, it also removes the default sun of a world with no light. When the "
        "host and a script both set the environment, the later write wins: the host's call happens between frames, "
        "the script's at the sync point inside the next frame.");
    ok &= registry->Add<&EnvironmentGet>("environment", "get", ThreadRule::WorldRead, "",
                                         "Returns a table with `ambient_color`, `ambient_intensity`, `sky_color` and "
                                         "`exposure` as of the tick start.");
    ok &= registry->Add<&LightSet>(
        "light", "set", ThreadRule::WorldWrite, "e, fields",
        "Changes fields of the light of entity `e` at the sync point: any of `color` (a vec3, linear RGB, at least "
        "0), `intensity` (at least 0), `radius` (above 0; a point or spot light's reach), `inner_cone` and "
        "`outer_cone` (a spot light's half-angles in radians, 0 <= inner < outer <= pi/2). A field left out keeps its "
        "value. The entity must already have a light (a level's light, one the host made, or `light.create`); an "
        "unknown field, a value out of range or an entity without a light is an error. A directional or spot light "
        "shines along its entity's -Z, so `transform.set_rotation` aims it.");
    ok &= registry->Add<&LightCreate>(
        "light", "create", ThreadRule::WorldWrite, "e, kind, fields",
        "Puts a light on entity `e` at the sync point: `kind` is `\"directional\"`, `\"point\"` or `\"spot\"`, and "
        "`fields` (a table or nil) takes the same fields as `light.set`, the rest being the defaults (white, intensity "
        "1, radius 10, cones 0 and pi/4). The entity needs a transform and no light yet.");
    return ok;
}

}  // namespace vse::engine_c

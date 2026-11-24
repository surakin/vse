#include "engine_c/script_scene.h"

#include "animation/components.h"
#include "engine_c/instances.h"
#include "engine_c/engine_internal.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "sync/mutex.h"
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
constexpr u32 kEnvReflection = 1u << 4;
constexpr u32 kEnvSkyMode = 1u << 5;
constexpr u32 kEnvSkyZenith = 1u << 6;
constexpr u32 kEnvSkyHorizon = 1u << 7;
constexpr u32 kEnvSkyGround = 1u << 8;
constexpr u32 kEnvSunDirection = 1u << 9;
constexpr u32 kEnvSunColor = 1u << 10;
constexpr u32 kEnvSunSize = 1u << 11;
constexpr u32 kEnvSkyHorizonBlend = 1u << 12;
constexpr u32 kEnvSkyExposure = 1u << 13;
constexpr u32 kEnvSkyAmbient = 1u << 14;
constexpr u32 kEnvSkyRotation = 1u << 15;
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
    f32 reflection_intensity;
    f32 sky_mode;  // ENGINE_SKY_*, a whole number
    f32 sky_zenith[3];
    f32 sky_horizon[3];
    f32 sky_ground[3];
    f32 sun_direction[3];
    f32 sun_color[3];
    f32 sun_size;
    f32 sky_horizon_blend;
    f32 sky_exposure;
    f32 sky_ambient_intensity;
    f32 sky_rotation;
};

struct SkyCubePayload
{
    u64 texture;
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
    // Checked in double first: a cast to f32 of a value beyond its range is undefined.
    if (!v->IsNumber() || !(v->ToDouble() >= -3.4028234663852886e38 && v->ToDouble() <= 3.4028234663852886e38))
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
    if ((p.mask & kEnvReflection) != 0 && !(SceneFinite(p.reflection_intensity) && p.reflection_intensity >= 0.0f))
    {
        script::RaiseScriptError("the environment's `reflection_intensity` must be finite and at least 0");
        return false;
    }
    if ((p.mask & kEnvExposure) != 0 && !(SceneFinite(p.exposure) && p.exposure > 0.0f))
    {
        script::RaiseScriptError("the environment's `exposure` must be finite and above 0");
        return false;
    }
    if ((p.mask & kEnvSkyMode) != 0 && !(p.sky_mode == 0.0f || p.sky_mode == 1.0f || p.sky_mode == 2.0f))
    {
        script::RaiseScriptError(
            "the environment's `sky_mode` must be 0 (the flat `sky_color`), 1 (none) or 2 (procedural)");
        return false;
    }
    const struct
    {
        u32 bit;
        const f32* value;
        const char* name;
    } colors[] = {{kEnvSkyZenith, p.sky_zenith, "sky_zenith"},
                  {kEnvSkyHorizon, p.sky_horizon, "sky_horizon"},
                  {kEnvSkyGround, p.sky_ground, "sky_ground"},
                  {kEnvSunColor, p.sun_color, "sun_color"}};
    for (const auto& c : colors)
    {
        if ((p.mask & c.bit) != 0 && !SceneColorValid(c.value))
        {
            script::RaiseScriptError("a sky or sun colour of the environment must be finite and at least 0");
            return false;
        }
    }
    if ((p.mask & kEnvSunDirection) != 0 &&
        !(p.sun_direction[0] * p.sun_direction[0] + p.sun_direction[1] * p.sun_direction[1] +
              p.sun_direction[2] * p.sun_direction[2] >
          1e-12f))
    {
        script::RaiseScriptError("the environment's `sun_direction` must not be zero");
        return false;
    }
    if ((p.mask & kEnvSunSize) != 0 && !(p.sun_size > 0.0f && p.sun_size <= kMaxSpotCone))
    {
        script::RaiseScriptError("the environment's `sun_size` must be in (0, pi / 2]");
        return false;
    }
    if ((p.mask & kEnvSkyHorizonBlend) != 0 && !(p.sky_horizon_blend > 0.0f && p.sky_horizon_blend <= 1.0f))
    {
        script::RaiseScriptError("the environment's `sky_horizon_blend` must be in (0, 1]");
        return false;
    }
    if ((p.mask & kEnvSkyExposure) != 0 && !(p.sky_exposure > 0.0f))
    {
        script::RaiseScriptError("the environment's `sky_exposure` must be above 0");
        return false;
    }
    if ((p.mask & kEnvSkyAmbient) != 0 && !(p.sky_ambient_intensity >= 0.0f))
    {
        script::RaiseScriptError("the environment's `sky_ambient_intensity` must be at least 0");
        return false;
    }
    if ((p.mask & kEnvSkyRotation) != 0 && !SceneFinite(p.sky_rotation))
    {
        script::RaiseScriptError("the environment's `sky_rotation` must be finite");
        return false;
    }
    return true;
}

void MergeEnvironment(Environment& e, const EnvironmentPayload& p);

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
    static const char* const kAllowed[] = {"ambient_color",
                                           "ambient_intensity",
                                           "sky_color",
                                           "exposure",
                                           "reflection_intensity",
                                           "sky_mode",
                                           "sky_zenith",
                                           "sky_horizon",
                                           "sky_ground",
                                           "sun_direction",
                                           "sun_color",
                                           "sun_size",
                                           "sky_horizon_blend",
                                           "sky_exposure",
                                           "sky_ambient_intensity",
                                           "sky_rotation",
                                           nullptr};
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
        !SceneReadNumber(fields, StringView("environment"), StringView("reflection_intensity"), kEnvReflection,
                         &p.reflection_intensity, &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sky_mode"), kEnvSkyMode, &p.sky_mode,
                         &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sky_zenith"), kEnvSkyZenith, p.sky_zenith,
                       &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sky_horizon"), kEnvSkyHorizon, p.sky_horizon,
                       &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sky_ground"), kEnvSkyGround, p.sky_ground,
                       &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sun_direction"), kEnvSunDirection,
                       p.sun_direction, &p.mask) ||
        !SceneReadVec3(fields, StringView("environment"), StringView("sun_color"), kEnvSunColor, p.sun_color,
                       &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sun_size"), kEnvSunSize, &p.sun_size,
                         &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sky_horizon_blend"), kEnvSkyHorizonBlend,
                         &p.sky_horizon_blend, &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sky_exposure"), kEnvSkyExposure,
                         &p.sky_exposure, &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sky_ambient_intensity"), kEnvSkyAmbient,
                         &p.sky_ambient_intensity, &p.mask) ||
        !SceneReadNumber(fields, StringView("environment"), StringView("sky_rotation"), kEnvSkyRotation,
                         &p.sky_rotation, &p.mask) ||
        !EnvironmentValid(p))
    {
        return;
    }
    {
        // The fields are checked one by one above; the result must be a usable environment as well (a procedural sky
        // over a sun size the host left at 0 is not).
        const SceneScriptServices* services = SceneServicesOf(context);
        Environment merged = services->object->environment;
        MergeEnvironment(merged, p);
        if (!EnvironmentUsable(merged))
        {
            script::RaiseScriptError(
                "the environment would not be valid: a procedural sky needs a non-zero `sun_direction`, `sun_size` "
                "in (0, pi / 2], `sky_horizon_blend` in (0, 1] and `sky_exposure` above 0; set them in the same call");
            return;
        }
    }
    if (p.mask != 0)
    {
        SceneRecord(context, kCommandEnvironmentSet, ecs::Entity(), &p, sizeof(p));
    }
}

void EnvironmentSetSkyCube(i64 texture)
{
    PartitionContext* context = SceneContext();
    if (context == nullptr || SceneServicesOf(context) == nullptr)
    {
        return;
    }
    if (texture < 0)
    {
        script::RaiseScriptError("environment.set_sky_cube takes a texture resource id, or 0 for none");
        return;
    }
    SkyCubePayload p = {};
    p.texture = static_cast<u64>(texture);
    SceneRecord(context, kCommandSkyCubeSet, ecs::Entity(), &p, sizeof(p));
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
    map.Set("reflection_intensity", Value::FromFloat(e.reflection_intensity));
    map.Set("sky_mode", Value::FromFloat(static_cast<f32>(e.sky_mode)));
    map.Set("sky_zenith", Value::FromVec3(Vec3(e.sky_zenith[0], e.sky_zenith[1], e.sky_zenith[2])));
    map.Set("sky_horizon", Value::FromVec3(Vec3(e.sky_horizon[0], e.sky_horizon[1], e.sky_horizon[2])));
    map.Set("sky_ground", Value::FromVec3(Vec3(e.sky_ground[0], e.sky_ground[1], e.sky_ground[2])));
    map.Set("sun_direction", Value::FromVec3(Vec3(e.sun_direction[0], e.sun_direction[1], e.sun_direction[2])));
    map.Set("sun_color", Value::FromVec3(Vec3(e.sun_color[0], e.sun_color[1], e.sun_color[2])));
    map.Set("sun_size", Value::FromFloat(e.sun_size));
    map.Set("sky_rotation", Value::FromFloat(e.sky_rotation));
    map.Set("sky_horizon_blend", Value::FromFloat(e.sky_horizon_blend));
    map.Set("sky_exposure", Value::FromFloat(e.sky_exposure));
    map.Set("sky_ambient_intensity", Value::FromFloat(e.sky_ambient_intensity));
    return map;
}

// Writes the named fields of `p` into `e`.
void MergeEnvironment(Environment& e, const EnvironmentPayload& p)
{
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
    if ((p.mask & kEnvReflection) != 0)
    {
        e.reflection_intensity = p.reflection_intensity;
    }
    if ((p.mask & kEnvSkyMode) != 0)
    {
        e.sky_mode = static_cast<u32>(p.sky_mode);
    }
    if ((p.mask & kEnvSkyZenith) != 0)
    {
        MemCopy(e.sky_zenith, p.sky_zenith, sizeof(e.sky_zenith));
    }
    if ((p.mask & kEnvSkyHorizon) != 0)
    {
        MemCopy(e.sky_horizon, p.sky_horizon, sizeof(e.sky_horizon));
    }
    if ((p.mask & kEnvSkyGround) != 0)
    {
        MemCopy(e.sky_ground, p.sky_ground, sizeof(e.sky_ground));
    }
    if ((p.mask & kEnvSunDirection) != 0)
    {
        MemCopy(e.sun_direction, p.sun_direction, sizeof(e.sun_direction));
    }
    if ((p.mask & kEnvSunColor) != 0)
    {
        MemCopy(e.sun_color, p.sun_color, sizeof(e.sun_color));
    }
    if ((p.mask & kEnvSunSize) != 0)
    {
        e.sun_size = p.sun_size;
    }
    if ((p.mask & kEnvSkyHorizonBlend) != 0)
    {
        e.sky_horizon_blend = p.sky_horizon_blend;
    }
    if ((p.mask & kEnvSkyExposure) != 0)
    {
        e.sky_exposure = p.sky_exposure;
    }
    if ((p.mask & kEnvSkyAmbient) != 0)
    {
        e.sky_ambient_intensity = p.sky_ambient_intensity;
    }
    if ((p.mask & kEnvSkyRotation) != 0)
    {
        e.sky_rotation = p.sky_rotation;
    }
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
    // The environment as the command would leave it: refused whole when it is not usable (a sky mode switched on over
    // fields a host left at zero), so no invalid environment is ever stored.
    Environment merged = services->object->environment;
    MergeEnvironment(merged, p);
    if (!EnvironmentUsable(merged))
    {
        return false;
    }
    services->object->environment = merged;
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

// ------------------------------------------------------------------ instanced sets and trees

// Plain numbers only, zeroed before use, so the payload has no indeterminate padding.
struct InstancePayload
{
    u64 target;  // the set or tree id
    u64 id;      // the instance id, or the node index
    f64 position[3];
    f32 rotation[4];
    f32 scale[3];
    f32 tint[4];
    u32 has_tint;
    u32 pad;
};

WorldObject* InstanceWorld(PartitionContext* context)
{
    const SceneScriptServices* services = SceneServicesOf(context);
    return services != nullptr ? services->object : nullptr;
}

// Reads the transform arguments into a payload and checks them as the C API does.
bool InstanceRead(i64 target, i64 id, Vec3d position, Quaternion rotation, Vec3 scale, const Value* tint,
                  InstancePayload* p)
{
    if (target <= 0 || id < 0)
    {
        script::RaiseScriptError("a set, tree, instance or node id is not valid");
        return false;
    }
    p->target = static_cast<u64>(target);
    p->id = static_cast<u64>(id);
    p->position[0] = position.x;
    p->position[1] = position.y;
    p->position[2] = position.z;
    p->rotation[0] = rotation.x;
    p->rotation[1] = rotation.y;
    p->rotation[2] = rotation.z;
    p->rotation[3] = rotation.w;
    p->scale[0] = scale.x;
    p->scale[1] = scale.y;
    p->scale[2] = scale.z;
    p->tint[0] = p->tint[1] = p->tint[2] = p->tint[3] = 1.0f;
    if (tint != nullptr && tint->Kind() != ValueKind::Nil)
    {
        if (tint->Kind() != ValueKind::Vec4)
        {
            script::RaiseScriptError("the tint must be a vec4 (red, green, blue, alpha) or nil");
            return false;
        }
        const Vec4 t = tint->AsVec4();
        p->tint[0] = t.x;
        p->tint[1] = t.y;
        p->tint[2] = t.z;
        p->tint[3] = t.w;
        p->has_tint = 1;
    }
    engine_instance item = {};
    for (u32 k = 0; k < 3; ++k)
    {
        item.position[k] = p->position[k];
        item.scale[k] = p->scale[k];
    }
    for (u32 k = 0; k < 4; ++k)
    {
        item.rotation[k] = p->rotation[k];
        item.tint[k] = p->tint[k];
    }
    renderer::InstanceTransform t;
    if (ReadInstanceItem(&item, sizeof(item), &t, nullptr) != ENGINE_OK)
    {
        script::RaiseScriptError(
            "an instance value is not finite or out of range, the rotation is not a unit quaternion, or a scale is "
            "not above 0");
        return false;
    }
    return true;
}

void ToItem(const InstancePayload& p, engine_instance* item)
{
    for (u32 k = 0; k < 3; ++k)
    {
        item->position[k] = p.position[k];
        item->scale[k] = p.scale[k];
    }
    for (u32 k = 0; k < 4; ++k)
    {
        item->rotation[k] = p.rotation[k];
        item->tint[k] = p.tint[k];
    }
}

void InstancesAdd(i64 set, Vec3d position, Quaternion rotation, Vec3 scale, Value tint)
{
    PartitionContext* context = SceneContext();
    WorldObject* world = context != nullptr ? InstanceWorld(context) : nullptr;
    if (world == nullptr)
    {
        return;
    }
    InstancePayload p = {};
    if (!InstanceRead(set, 0, position, rotation, scale, &tint, &p))
    {
        return;
    }
    if (FindInstanceSet(world->instances, p.target) == nullptr)
    {
        script::RaiseScriptError("instances.add: no such set");
        return;
    }
    SceneRecord(context, kCommandInstancesAdd, ecs::Entity(), &p, sizeof(p));
}

void InstancesSet(i64 set, i64 id, Vec3d position, Quaternion rotation, Vec3 scale)
{
    PartitionContext* context = SceneContext();
    WorldObject* world = context != nullptr ? InstanceWorld(context) : nullptr;
    if (world == nullptr)
    {
        return;
    }
    InstancePayload p = {};
    if (!InstanceRead(set, id, position, rotation, scale, nullptr, &p))
    {
        return;
    }
    if (FindInstanceSet(world->instances, p.target) == nullptr)
    {
        script::RaiseScriptError("instances.set: no such set");
        return;
    }
    SceneRecord(context, kCommandInstancesSet, ecs::Entity(), &p, sizeof(p));
}

void InstancesRemove(i64 set, i64 id)
{
    PartitionContext* context = SceneContext();
    WorldObject* world = context != nullptr ? InstanceWorld(context) : nullptr;
    if (world == nullptr)
    {
        return;
    }
    if (set <= 0 || id < 0 || FindInstanceSet(world->instances, static_cast<u64>(set)) == nullptr)
    {
        script::RaiseScriptError("instances.remove: no such set");
        return;
    }
    InstancePayload p = {};
    p.target = static_cast<u64>(set);
    p.id = static_cast<u64>(id);
    SceneRecord(context, kCommandInstancesRemove, ecs::Entity(), &p, sizeof(p));
}

void TreeSetLocal(i64 tree, i64 node, Vec3d position, Quaternion rotation, Vec3 scale)
{
    PartitionContext* context = SceneContext();
    WorldObject* world = context != nullptr ? InstanceWorld(context) : nullptr;
    if (world == nullptr)
    {
        return;
    }
    InstancePayload p = {};
    if (!InstanceRead(tree, node, position, rotation, scale, nullptr, &p))
    {
        return;
    }
    WorldInstanceTree* entry = FindInstanceTree(world->instances, p.target);
    if (entry == nullptr)
    {
        script::RaiseScriptError("tree.set_local: no such tree");
        return;
    }
    renderer::TreeNodeId id;
    id.value = static_cast<u32>(p.id);
    if (p.id >= entry->tree->NodeCount() || entry->tree->Find(id) == renderer::kTreeNone)
    {
        script::RaiseScriptError("tree.set_local: the tree has no such node");
        return;
    }
    SceneRecord(context, kCommandTreeSetLocal, ecs::Entity(), &p, sizeof(p));
}

WorldObject* MergeWorld(MergeContext& context)
{
    const script::MergeServices* merge = static_cast<const script::MergeServices*>(context.User());
    const SceneScriptServices* services = merge != nullptr ? static_cast<const SceneScriptServices*>(
                                                                 merge->extensions[script::kServiceScene])
                                                           : nullptr;
    return services != nullptr ? services->object : nullptr;
}

bool ApplyInstancesAdd(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    InstancePayload p;
    WorldObject* world = MergeWorld(context);
    if (world == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    WorldInstanceSet* entry = FindInstanceSet(world->instances, p.target);
    if (entry == nullptr)
    {
        return false;
    }
    engine_instance item = {};
    ToItem(p, &item);
    renderer::InstanceTransform t;
    renderer::InstanceExtra extra;
    if (ReadInstanceItem(&item, sizeof(item), &t, &extra) != ENGINE_OK)
    {
        return false;
    }
    renderer::InstanceId id;
    return entry->set->Add(t, &extra, &id) == renderer::InstanceStatus::Ok;
}

bool ApplyInstancesSet(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    InstancePayload p;
    WorldObject* world = MergeWorld(context);
    if (world == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    WorldInstanceSet* entry = FindInstanceSet(world->instances, p.target);
    if (entry == nullptr)
    {
        return false;
    }
    engine_instance item = {};
    ToItem(p, &item);
    renderer::InstanceTransform t;
    if (ReadInstanceItem(&item, sizeof(item), &t, nullptr) != ENGINE_OK)
    {
        return false;
    }
    renderer::InstanceId id;
    id.index = static_cast<u32>(p.id & 0xFFFFFFFFu);
    id.generation = static_cast<u32>(p.id >> 32);
    return entry->set->SetTransform(id, t) == renderer::InstanceStatus::Ok;
}

bool ApplySkyCubeSet(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const script::MergeServices* merge = static_cast<const script::MergeServices*>(context.User());
    const SceneScriptServices* services = merge != nullptr ? static_cast<const SceneScriptServices*>(
                                                                 merge->extensions[script::kServiceScene])
                                                           : nullptr;
    SkyCubePayload p;
    if (services == nullptr || services->object == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    if (p.texture != 0)
    {  // as `world_set_sky_cube`: the resource must be a mounted texture
        Runtime* runtime = CurrentRuntime();
        engine_resource_info info = {};
        info.struct_size = sizeof(info);
        if (runtime == nullptr || runtime->Resources()->Mounted(p.texture, &info) != ENGINE_OK ||
            info.type != ENGINE_RESOURCE_TEXTURE)
        {
            return false;
        }
    }
    services->object->environment.sky_cube = p.texture;
    services->object->environment_set = true;
    return true;
}

bool ApplyInstancesRemove(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    InstancePayload p;
    WorldObject* world = MergeWorld(context);
    if (world == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    WorldInstanceSet* entry = FindInstanceSet(world->instances, p.target);
    if (entry == nullptr)
    {
        return false;
    }
    renderer::InstanceId id;
    id.index = static_cast<u32>(p.id & 0xFFFFFFFFu);
    id.generation = static_cast<u32>(p.id >> 32);
    return entry->set->Remove(id) == renderer::InstanceStatus::Ok;
}

bool ApplyTreeSetLocal(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    InstancePayload p;
    WorldObject* world = MergeWorld(context);
    if (world == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    WorldInstanceTree* entry = FindInstanceTree(world->instances, p.target);
    if (entry == nullptr)
    {
        return false;
    }
    engine_instance item = {};
    ToItem(p, &item);
    renderer::InstanceTransform t;
    if (ReadInstanceItem(&item, sizeof(item), &t, nullptr) != ENGINE_OK)
    {
        return false;
    }
    renderer::TreeNodeId id;
    id.value = static_cast<u32>(p.id);
    return entry->tree->SetLocal(id, t.position, t.rotation, t.scale) == renderer::TreeStatus::Ok;
}

usize SceneNameOf(void*, const ecs::World& world, ecs::Entity entity, char* out, usize capacity)
{
    const EntityName* name = world.Peek<EntityName>(entity);
    if (name == nullptr)
    {
        return 0;
    }
    const usize length = name->length < capacity ? name->length : capacity;
    MemCopy(out, name->text, length);
    return length;
}

ecs::Entity SceneFindByName(void* user, const ecs::World& world, StringView name)
{
    SceneNames* self = static_cast<SceneNames*>(user);
    LockGuard<Mutex> lock(self->mutex);
    ecs::Entity found;
    // The scripts read the tick-start world: nothing writes it while they run, and the query only reads.
    auto query = const_cast<ecs::World&>(world).Query<ecs::Read<EntityName>>();
    query.ForEach(
        [&](ecs::Entity entity, const EntityName& candidate)
        {
            if (!found.IsValid() && StringView(candidate.text, candidate.length) == name)
            {
                found = entity;
            }
        });
    return found;
}

}  // namespace

SceneNames::SceneNames()
{
    services.name_of = &SceneNameOf;
    services.find = &SceneFindByName;
    services.user = this;
}

// ---- joints

struct JointRotationPayload
{
    u32 joint;
    f32 rotation[4];
};

// The skeleton an entity is drawn with, or null (not an entity of the world as of the tick start, no skeleton
// instance, or a skeleton that is not loaded).
const asset::SkeletonView* SkeletonOfEntity(const SceneScriptServices* services, const ecs::World& world,
                                            ecs::Entity entity)
{
    if (entity.IsPlaceholder() || services->runtime == nullptr)
    {
        return nullptr;
    }
    const animation::SkeletonInstance* instance = world.Get<animation::SkeletonInstance>(entity);
    ResourceHost* resources = instance != nullptr ? services->runtime->ResourcesIfCreated() : nullptr;
    return resources == nullptr ? nullptr
                                : resources->Animation().FindSkeleton(asset::ResourceId::FromValue(instance->skeleton));
}

Value SkeletonFindJoint(ecs::Entity e, StringView name)
{
    PartitionContext* context = SceneContext();
    const SceneScriptServices* services = context != nullptr ? SceneServicesOf(context) : nullptr;
    if (services == nullptr)
    {
        return Value();
    }
    if (services->runtime == nullptr)
    {
        script::RaiseScriptError("skeleton.find_joint: this world has no engine runtime to read skeletons from");
        return Value();
    }
    u32 index = 0;
    const asset::SkeletonView* skeleton = SkeletonOfEntity(services, *context->world, e);
    if (skeleton == nullptr || name.Length() == 0 || name.Length() > 256 || !skeleton->FindJoint(name, &index))
    {
        return Value();
    }
    return Value::FromInt(static_cast<i64>(index));
}

void AnimationSetJointRotation(ecs::Entity e, i64 joint, Quaternion rotation)
{
    PartitionContext* context = SceneContext();
    const SceneScriptServices* services = context != nullptr ? SceneServicesOf(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (services->runtime == nullptr)
    {
        script::RaiseScriptError(
            "animation.set_joint_rotation: this world has no engine runtime to read skeletons from");
        return;
    }
    const asset::SkeletonView* skeleton = SkeletonOfEntity(services, *context->world, e);
    if (skeleton == nullptr)
    {
        script::RaiseScriptError("animation.set_joint_rotation: the entity has no loaded skeleton instance");
        return;
    }
    if (joint < 0 || static_cast<u64>(joint) >= skeleton->JointCount() ||
        static_cast<u64>(joint) >= animation::kMaxPoseJoints)
    {
        script::RaiseScriptError("animation.set_joint_rotation: `joint` is not a joint of the entity's skeleton");
        return;
    }
    Array<f32, 4> raw = {{rotation.x, rotation.y, rotation.z, rotation.w}};
    Array<f32, 4> unit;
    const animation::HostRotationCheck checked = animation::CheckHostRotation(raw, &unit);
    if (checked != animation::HostRotationCheck::Ok)
    {
        script::RaiseScriptError(checked == animation::HostRotationCheck::NotFinite
                                     ? "animation.set_joint_rotation: `quaternion` is not finite"
                                     : "animation.set_joint_rotation: `quaternion` is not a unit quaternion");
        return;
    }
    JointRotationPayload p{};
    p.joint = static_cast<u32>(joint);
    for (u32 c = 0; c < 4; ++c)
    {
        p.rotation[c] = unit[c];
    }
    SceneRecord(context, kCommandJointRotation, e, &p, sizeof(p));
}

u32 JointRotationConflictKey(Span<const u8> payload)
{
    JointRotationPayload p;
    if (payload.Size() != sizeof(p))
    {
        return 0;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    return p.joint + 1;
}

bool ApplyJointRotation(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    JointRotationPayload p;
    const animation::SkeletonInstance* instance = context.World()->Get<animation::SkeletonInstance>(target);
    if (instance == nullptr || payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    const script::MergeServices* merge = static_cast<const script::MergeServices*>(context.User());
    const SceneScriptServices* services = merge != nullptr ? static_cast<const SceneScriptServices*>(
                                                                 merge->extensions[script::kServiceScene])
                                                           : nullptr;
    const asset::SkeletonView* skeleton = services != nullptr ? SkeletonOfEntity(services, *context.World(), target)
                                                              : nullptr;
    Array<f32, 4> raw = {{p.rotation[0], p.rotation[1], p.rotation[2], p.rotation[3]}};
    Array<f32, 4> unit;
    if (skeleton == nullptr || p.joint >= skeleton->JointCount() || p.joint >= animation::kMaxPoseJoints ||
        animation::CheckHostRotation(raw, &unit) != animation::HostRotationCheck::Ok)
    {
        return false;
    }
    if (animation::JointRotations* have = context.World()->Get<animation::JointRotations>(target))
    {
        animation::SetHostJointRotation(have, p.joint, unit);
        return true;
    }
    if (animation::IsIdentityRotation(unit))
    {
        return true;
    }
    animation::JointRotations fresh;
    animation::SetHostJointRotation(&fresh, p.joint, unit);
    return context.World()->Add(target, Move(fresh)) == ecs::Status::Ok;
}

bool RegisterSceneCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandEnvironmentSet,
                          CommandKind{"environment.set", &ApplyEnvironmentSet, false, false, true});
    ok &= kinds->Register(kCommandSkyCubeSet,
                          CommandKind{"environment.set_sky_cube", &ApplySkyCubeSet, false, false, true});
    ok &= kinds->Register(kCommandLightSet, CommandKind{"light.set", &ApplyLightSet, false, true});
    ok &= kinds->Register(kCommandLightCreate, CommandKind{"light.create", &ApplyLightCreate, false, true});
    ok &= kinds->Register(kCommandInstancesAdd, CommandKind{"instances.add", &ApplyInstancesAdd, false, false, true});
    ok &= kinds->Register(kCommandInstancesSet, CommandKind{"instances.set", &ApplyInstancesSet, false, false, true});
    ok &= kinds->Register(kCommandInstancesRemove,
                          CommandKind{"instances.remove", &ApplyInstancesRemove, false, false, true});
    ok &= kinds->Register(kCommandTreeSetLocal, CommandKind{"tree.set_local", &ApplyTreeSetLocal, false, false, true});
    ok &= kinds->Register(kCommandJointRotation, CommandKind{"animation.set_joint_rotation", &ApplyJointRotation, false,
                                                             true, false, &JointRotationConflictKey});
    return ok;
}

bool RegisterSceneBindings(script::BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&EnvironmentSet>(
        "environment", "set", ThreadRule::WorldWrite, "fields",
        "Changes the world's environment at the sync point; every surface that shows the world uses it from its next "
        "frame. `fields` is a table with any of `ambient_color` (a vec3, linear RGB, at least 0), `ambient_intensity` "
        "(at least 0), `sky_color` (a vec3, at least 0), `exposure` (above 0) and `reflection_intensity` (at least 0: "
        "how much of the sky the lit surfaces reflect with a Fresnel term, 0 by default) and the sky: `sky_mode` (0 "
        "draws the flat `sky_color`, 1 nothing, 2 a procedural sky), `sky_zenith`, `sky_horizon`, `sky_ground`, "
        "`sun_color` (vec3s, at least 0), `sun_direction` (a vec3, not zero), `sun_size` (radians, above 0, at most "
        "pi/2), `sky_horizon_blend` (above 0, at most 1), `sky_exposure` (above 0) and `sky_ambient_intensity` (at "
        "least 0: the sky's blurred colour as ambient light, replacing `ambient_color` when above 0) and `sky_rotation` "
        "(radians, turns a cube sky about +Y); a field left out keeps its "
        "value, and calling it every tick is fine. An unknown field or a value out of range is an error naming it. As "
        "`world_set_environment` in the C API, it also removes the default sun of a world with no light. When the "
        "host and a script both set the environment, the later write wins: the host's call happens between frames, "
        "the script's at the sync point inside the next frame.");
    ok &= registry->Add<&EnvironmentSetSkyCube>(
        "environment", "set_sky_cube", ThreadRule::WorldWrite, "texture",
        "Draws the sky from the cube texture resource with id `texture` (a minted cube or environment texture the host "
        "added), or from the gradient again when `texture` is 0, at the sync point. The cube turns by "
        "`environment.set({sky_rotation = radians})` and shows while `sky_mode` is 2. A resource that is not mounted "
        "or is not a texture counts a failed command and changes nothing.");
    ok &= registry->Add<&EnvironmentGet>(
        "environment", "get", ThreadRule::WorldRead, "",
        "Returns a table with `ambient_color`, `ambient_intensity`, `sky_color`, "
        "`exposure`, `reflection_intensity` and the sky fields (`sky_mode` ... `sky_ambient_intensity`) as of the tick "
        "start.");
    ok &= registry->Add<&LightSet>(
        "light", "set", ThreadRule::WorldWrite, "e, fields",
        "Changes fields of the light of entity `e` at the sync point: any of `color` (a vec3, linear RGB, at least "
        "0), `intensity` (at least 0), `radius` (above 0; a point or spot light's reach), `inner_cone` and "
        "`outer_cone` (a spot light's half-angles in radians, 0 <= inner < outer <= pi/2). A field left out keeps its "
        "value. Each call is checked against the light as of the tick start, so set both cones of a spot light in one call. The entity must already have a light (a level's light, one the host made, or `light.create`); an "
        "unknown field, a value out of range or an entity without a light is an error. A directional or spot light "
        "shines along its entity's -Z, so `transform.set_rotation` aims it.");
    ok &= registry->Add<&LightCreate>(
        "light", "create", ThreadRule::WorldWrite, "e, kind, fields",
        "Puts a light on entity `e` at the sync point: `kind` is `\"directional\"`, `\"point\"` or `\"spot\"`, and "
        "`fields` (a table or nil) takes the same fields as `light.set`, the rest being the defaults (white, intensity "
        "1, radius 10, cones 0 and pi/4). The entity needs a transform and no light yet.");
    ok &= registry->Add<&InstancesAdd>(
        "instances", "add", ThreadRule::WorldWrite, "set, position, rotation, scale, tint",
        "Adds an instance to the instance set `set` (the id the host got from `instance_set_create`) at the sync "
        "point: `position` a vec3 (exact, metres), `rotation` a unit quaternion, `scale` a vec3 above 0 and `tint` a "
        "vec4 colour multiplier or nil for white. Adds from several scripts apply in the fixed merge order, so the "
        "set is the same for any number of threads. An unknown set, a value out of range or a rotation that is not a "
        "unit quaternion is an error; a full set drops the instance and counts a failed command.");
    ok &= registry->Add<&InstancesSet>(
        "instances", "set", ThreadRule::WorldWrite, "set, id, position, rotation, scale",
        "Moves instance `id` (the id `instance_set_add_many` returned) of the set `set` at the sync point; the tint "
        "is kept. The values are checked as for `instances.add`; an instance that was removed counts a failed "
        "command at the sync point.");
    ok &= registry->Add<&InstancesRemove>(
        "instances", "remove", ThreadRule::WorldWrite, "set, id",
        "Removes instance `id` of the set `set` at the sync point; an unknown set is an error, an instance already "
        "removed counts a failed command.");
    ok &= registry->Add<&TreeSetLocal>(
        "tree", "set_local", ThreadRule::WorldWrite, "tree, node, position, rotation, scale",
        "Sets the local transform of node `node` (its index in the array the host made the tree from) of the "
        "instance tree `tree` at the sync point (a root's `position` is its world position, any other node's the "
        "offset from its parent); only that node's subtree is recomposed at the next frame. The values are checked "
        "as for `instances.add`; an unknown tree or node is an error.");
    ok &= registry->Add<&SkeletonFindJoint>(
        "skeleton", "find_joint", ThreadRule::WorldRead, "e, name",
        "Returns the index of the joint called `name` in the skeleton entity `e` is drawn with, as of the tick start, "
        "or nil when the entity has no loaded skeleton instance or the skeleton has no such joint.");
    ok &= registry->Add<&AnimationSetJointRotation>(
        "animation", "set_joint_rotation", ThreadRule::WorldWrite, "e, joint, quaternion",
        "Adds the rotation `quaternion` (a unit quat) to joint `joint` of entity `e`'s skeleton at the sync point: "
        "after the clips are sampled and blended the joint's local rotation becomes the pose's rotation composed with "
        "this one (`pose * added`, about the joint's own axes). The identity takes it away; it stays until set again, "
        "and several calls for one joint in a tick are last-wins. A joint the skeleton does not have, an entity with "
        "no loaded skeleton instance, or a quaternion that is not finite or not of length 1 within 0.1% is an error.");
    return ok;
}

}  // namespace vse::engine_c

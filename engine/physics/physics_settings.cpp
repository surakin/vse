#include "physics/physics_settings.h"

#include "settings/setting.h"
#include "string_view.h"

namespace engine_placeholder::physics
{

void RegisterPhysicsSettings(settings::Registry* registry)
{
    settings::SettingDesc hz = settings::MakeFloat("physics.step_hz", 60.0, 1.0, 1000.0);
    hz.env = "ENGINE_PHYSICS_STEP_HZ";
    hz.flag = "physics-step-hz";
    hz.label = "Physics step rate";
    hz.description = "fixed steps per second (30 to 120 are stable; the state hash depends on it)";
    hz.group = "physics";
    registry->Register(hz);
    settings::SettingDesc substeps = settings::MakeInt("physics.max_substeps", 4, 1, 32);
    substeps.env = "ENGINE_PHYSICS_MAX_SUBSTEPS";
    substeps.flag = "physics-max-substeps";
    substeps.label = "Physics substep cap";
    substeps.description = "the most fixed steps one frame runs; a longer frame drops the rest";
    substeps.group = "physics";
    registry->Register(substeps);
    settings::SettingDesc gravity = settings::MakeFloat("physics.gravity_y", -9.81, -1000.0, 1000.0);
    gravity.env = "ENGINE_PHYSICS_GRAVITY_Y";
    gravity.flag = "physics-gravity-y";
    gravity.label = "Gravity";
    gravity.description = "the vertical acceleration in metres per second squared (negative is down)";
    gravity.group = "physics";
    registry->Register(gravity);
    settings::SettingDesc margin = settings::MakeFloat("physics.contact_margin", 0.02, 0.0, 1.0);
    margin.env = "ENGINE_PHYSICS_CONTACT_MARGIN";
    margin.flag = "physics-contact-margin";
    margin.label = "Contact margin";
    margin.description =
        "metres: shapes closer than this get a speculative contact, so resting contacts are stable and approaching ones are predicted";
    margin.group = "physics";
    margin.flags = settings::kAdvanced;
    registry->Register(margin);
    settings::SettingDesc interpolate = settings::MakeBool("physics.interpolate", true);
    interpolate.env = "ENGINE_PHYSICS_INTERPOLATE";
    interpolate.flag = "physics-interpolate";
    interpolate.label = "Interpolate bodies";
    interpolate
        .description = "draw bodies at the blend of their last two states, so any frame rate shows smooth motion";
    interpolate.group = "physics";
    registry->Register(interpolate);
}

StepConfig StepConfigFromSettings(const settings::Registry& registry)
{
    StepConfig config;
    const settings::SettingId hz = registry.Find(StringView("physics.step_hz"));
    if (hz.IsValid())
    {
        config.step_hz = static_cast<f32>(settings::Setting<f64>(registry, hz).Get());
    }
    const settings::SettingId substeps = registry.Find(StringView("physics.max_substeps"));
    if (substeps.IsValid())
    {
        config.max_substeps = static_cast<u32>(settings::Setting<i64>(registry, substeps).Get());
    }
    const settings::SettingId gravity = registry.Find(StringView("physics.gravity_y"));
    if (gravity.IsValid())
    {
        config.gravity = Vec3(0.0f, static_cast<f32>(settings::Setting<f64>(registry, gravity).Get()), 0.0f);
    }
    const settings::SettingId margin = registry.Find(StringView("physics.contact_margin"));
    if (margin.IsValid())
    {
        config.contact_margin = static_cast<f32>(settings::Setting<f64>(registry, margin).Get());
    }
    const settings::SettingId interpolate = registry.Find(StringView("physics.interpolate"));
    if (interpolate.IsValid())
    {
        config.interpolate = settings::Setting<bool>(registry, interpolate).Get();
    }
    return config;
}

}  // namespace engine_placeholder::physics

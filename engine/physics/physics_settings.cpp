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
    settings::SettingDesc iterations = settings::MakeInt("physics.velocity_iterations", 2, 1, 100);
    iterations.env = "ENGINE_PHYSICS_VELOCITY_ITERATIONS";
    iterations.flag = "physics-velocity-iterations";
    iterations.label = "Solver iterations";
    iterations.description = "contact solver passes per substep: more is stiffer stacks and slower steps";
    iterations.group = "physics";
    registry->Register(iterations);
    settings::SettingDesc solver_substeps = settings::MakeInt("physics.substeps", 4, 1, 16);
    solver_substeps.env = "ENGINE_PHYSICS_SUBSTEPS";
    solver_substeps.flag = "physics-substeps";
    solver_substeps.label = "Solver substeps";
    solver_substeps
        .description = "substeps the touching bodies take per step: more keeps tall stacks still and costs more";
    solver_substeps.group = "physics";
    registry->Register(solver_substeps);
    settings::SettingDesc hertz = settings::MakeFloat("physics.contact_hertz", 30.0, 0.0, 1000.0);
    hertz.env = "ENGINE_PHYSICS_CONTACT_HERTZ";
    hertz.flag = "physics-contact-hertz";
    hertz.label = "Contact stiffness";
    hertz.description =
        "the frequency of the spring a resting contact acts as (capped at a quarter of the substep rate)";
    hertz.group = "physics";
    hertz.flags = settings::kAdvanced;
    registry->Register(hertz);
    settings::SettingDesc slop = settings::MakeFloat("physics.penetration_slop", 0.001, 0.0, 0.1);
    slop.env = "ENGINE_PHYSICS_PENETRATION_SLOP";
    slop.flag = "physics-penetration-slop";
    slop.label = "Penetration slop";
    slop.description = "metres of overlap that are left alone, so resting contacts do not jitter";
    slop.group = "physics";
    slop.flags = settings::kAdvanced;
    registry->Register(slop);
    settings::SettingDesc max_velocity = settings::MakeFloat("physics.max_velocity", 100.0, 1.0, 100000.0);
    max_velocity.env = "ENGINE_PHYSICS_MAX_VELOCITY";
    max_velocity.flag = "physics-max-velocity";
    max_velocity.label = "Speed limit";
    max_velocity.description = "no body moves faster than this (metres per second)";
    max_velocity.group = "physics";
    max_velocity.flags = settings::kAdvanced;
    registry->Register(max_velocity);
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
    const settings::SettingId iterations = registry.Find(StringView("physics.velocity_iterations"));
    if (iterations.IsValid())
    {
        config.velocity_iterations = static_cast<u32>(settings::Setting<i64>(registry, iterations).Get());
    }
    const settings::SettingId solver_substeps = registry.Find(StringView("physics.substeps"));
    if (solver_substeps.IsValid())
    {
        config.substeps = static_cast<u32>(settings::Setting<i64>(registry, solver_substeps).Get());
    }
    const settings::SettingId hertz = registry.Find(StringView("physics.contact_hertz"));
    if (hertz.IsValid())
    {
        config.contact_hertz = static_cast<f32>(settings::Setting<f64>(registry, hertz).Get());
    }
    const settings::SettingId slop = registry.Find(StringView("physics.penetration_slop"));
    if (slop.IsValid())
    {
        config.contact_slop = static_cast<f32>(settings::Setting<f64>(registry, slop).Get());
    }
    const settings::SettingId max_velocity = registry.Find(StringView("physics.max_velocity"));
    if (max_velocity.IsValid())
    {
        config.max_velocity = static_cast<f32>(settings::Setting<f64>(registry, max_velocity).Get());
    }
    const settings::SettingId interpolate = registry.Find(StringView("physics.interpolate"));
    if (interpolate.IsValid())
    {
        config.interpolate = settings::Setting<bool>(registry, interpolate).Get();
    }
    return config;
}

}  // namespace engine_placeholder::physics

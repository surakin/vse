#include "physics/physics_settings.h"

#include "settings/setting.h"
#include "string_view.h"

namespace vse::physics
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
    settings::SettingDesc joint_hertz = settings::MakeFloat("physics.joint_hertz", 60.0, 0.0, 1000.0);
    joint_hertz.env = "ENGINE_PHYSICS_JOINT_HERTZ";
    joint_hertz.flag = "physics-joint-hertz";
    joint_hertz.label = "Joint stiffness";
    joint_hertz.description =
        "the frequency of the spring a joint's constraint acts as (capped at a quarter of the substep rate)";
    joint_hertz.group = "physics";
    joint_hertz.flags = settings::kAdvanced;
    registry->Register(joint_hertz);
    settings::SettingDesc ccd = settings::MakeFloat("physics.ccd_threshold", 0.0, 0.0, 100.0);
    ccd.env = "ENGINE_PHYSICS_CCD_THRESHOLD";
    ccd.flag = "physics-ccd-threshold";
    ccd.label = "Continuous collision threshold";
    ccd.description =
        "a body that moves farther than this fraction of its smallest extent in one step is swept (0: only flagged bodies)";
    ccd.group = "physics";
    ccd.flags = settings::kAdvanced;
    registry->Register(ccd);
    settings::SettingDesc threads = settings::MakeInt("physics.threads", 1, 1, 256);
    threads.env = "ENGINE_PHYSICS_THREADS";
    threads.flag = "physics-threads";
    threads.label = "Physics threads";
    threads.description = "1 runs the step inline; more lets it use the job scheduler (the results do not change)";
    threads.group = "physics";
    registry->Register(threads);
    settings::SettingDesc stay_events = settings::MakeBool("physics.stay_events", false);
    stay_events.env = "ENGINE_PHYSICS_STAY_EVENTS";
    stay_events.flag = "physics-stay-events";
    stay_events.label = "Contact stay events";
    stay_events.description = "whether a pair that keeps touching reports a stay event every step";
    stay_events.group = "physics";
    registry->Register(stay_events);
    settings::SettingDesc debug_draw = settings::MakeBool("physics.debug_draw", false);
    debug_draw.env = "ENGINE_PHYSICS_DEBUG_DRAW";
    debug_draw.flag = "physics-debug-draw";
    debug_draw.label = "Draw the physics world";
    debug_draw.description = "whether the program draws shapes, contacts and joints as debug lines";
    debug_draw.group = "physics";
    registry->Register(debug_draw);
    settings::SettingDesc allow_sleep = settings::MakeBool("physics.allow_sleep", true);
    allow_sleep.env = "ENGINE_PHYSICS_ALLOW_SLEEP";
    allow_sleep.flag = "physics-allow-sleep";
    allow_sleep.label = "Sleeping";
    allow_sleep.description = "whether islands of resting bodies fall asleep (a sleeping body costs almost nothing)";
    allow_sleep.group = "physics";
    registry->Register(allow_sleep);
    settings::SettingDesc sleep_linear = settings::MakeFloat("physics.sleep_linear", 0.05, 0.0, 10.0);
    sleep_linear.env = "ENGINE_PHYSICS_SLEEP_LINEAR";
    sleep_linear.flag = "physics-sleep-linear";
    sleep_linear.label = "Sleep linear speed";
    sleep_linear.description = "a body is at rest while it moves slower than this, in metres per second";
    sleep_linear.group = "physics";
    sleep_linear.flags = settings::kAdvanced;
    registry->Register(sleep_linear);
    settings::SettingDesc sleep_angular = settings::MakeFloat("physics.sleep_angular", 0.05, 0.0, 10.0);
    sleep_angular.env = "ENGINE_PHYSICS_SLEEP_ANGULAR";
    sleep_angular.flag = "physics-sleep-angular";
    sleep_angular.label = "Sleep angular speed";
    sleep_angular.description = "a body is at rest while it turns slower than this, in radians per second";
    sleep_angular.group = "physics";
    sleep_angular.flags = settings::kAdvanced;
    registry->Register(sleep_angular);
    settings::SettingDesc sleep_time = settings::MakeFloat("physics.sleep_time", 0.5, 0.0, 60.0);
    sleep_time.env = "ENGINE_PHYSICS_SLEEP_TIME";
    sleep_time.flag = "physics-sleep-time";
    sleep_time.label = "Sleep time";
    sleep_time.description = "seconds every body of an island must be at rest before the island falls asleep";
    sleep_time.group = "physics";
    sleep_time.flags = settings::kAdvanced;
    registry->Register(sleep_time);
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
    const settings::SettingId joint_hertz = registry.Find(StringView("physics.joint_hertz"));
    if (joint_hertz.IsValid())
    {
        config.joint_hertz = static_cast<f32>(settings::Setting<f64>(registry, joint_hertz).Get());
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
    const settings::SettingId ccd = registry.Find(StringView("physics.ccd_threshold"));
    if (ccd.IsValid())
    {
        config.ccd_threshold = static_cast<f32>(settings::Setting<f64>(registry, ccd).Get());
    }
    const settings::SettingId threads = registry.Find(StringView("physics.threads"));
    if (threads.IsValid())
    {
        config.threads = static_cast<u32>(settings::Setting<i64>(registry, threads).Get());
    }
    const settings::SettingId stay_events = registry.Find(StringView("physics.stay_events"));
    if (stay_events.IsValid())
    {
        config.stay_events = settings::Setting<bool>(registry, stay_events).Get();
    }
    const settings::SettingId debug_draw = registry.Find(StringView("physics.debug_draw"));
    if (debug_draw.IsValid())
    {
        config.debug_draw = settings::Setting<bool>(registry, debug_draw).Get();
    }
    const settings::SettingId allow_sleep = registry.Find(StringView("physics.allow_sleep"));
    if (allow_sleep.IsValid())
    {
        config.allow_sleep = settings::Setting<bool>(registry, allow_sleep).Get();
    }
    const settings::SettingId sleep_linear = registry.Find(StringView("physics.sleep_linear"));
    if (sleep_linear.IsValid())
    {
        config.sleep_linear = static_cast<f32>(settings::Setting<f64>(registry, sleep_linear).Get());
    }
    const settings::SettingId sleep_angular = registry.Find(StringView("physics.sleep_angular"));
    if (sleep_angular.IsValid())
    {
        config.sleep_angular = static_cast<f32>(settings::Setting<f64>(registry, sleep_angular).Get());
    }
    const settings::SettingId sleep_time = registry.Find(StringView("physics.sleep_time"));
    if (sleep_time.IsValid())
    {
        config.sleep_time = static_cast<f32>(settings::Setting<f64>(registry, sleep_time).Get());
    }
    const settings::SettingId interpolate = registry.Find(StringView("physics.interpolate"));
    if (interpolate.IsValid())
    {
        config.interpolate = settings::Setting<bool>(registry, interpolate).Get();
    }
    return config;
}

}  // namespace vse::physics

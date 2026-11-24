// The runtime bindings (Phase 27 Day 453): `random.*`, `log.*` and `time.*`. They read the partition context of the
// running job (partition_context.h) and nothing shared, so any number of partitions use them at once.
#include "lang/scalar.h"
#include "os/os.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/log_buffer.h"
#include "script/partition_context.h"

namespace vse::script
{

namespace
{

// Outside a partition job (a task state, a bare test state) there is no stream to read: a per-thread stream stands in
// so the calls still work, but its numbers are not tied to an entity.
thread_local u64 g_fallback_random = 0x1234567ull;

u64* RandomState()
{
    PartitionContext* context = CurrentPartitionContext();
    return context != nullptr ? &context->random_state : &g_fallback_random;
}

f32 Unit(u64 bits)
{
    return static_cast<f32>(bits >> 40) * (1.0f / 16777216.0f);  // 24 bits: exact in f32, in [0, 1)
}

f32 RandomFloat()
{
    return Unit(AdvanceRandom(RandomState()));
}

i32 RandomInt(i32 lo, i32 hi)
{
    if (hi < lo)
    {
        const i32 t = lo;
        lo = hi;
        hi = t;
    }
    const u64 span = static_cast<u64>(static_cast<i64>(hi) - static_cast<i64>(lo)) + 1;
    return static_cast<i32>(static_cast<i64>(lo) + static_cast<i64>(AdvanceRandom(RandomState()) % span));
}

// Rejection sampling in the unit cube, then scaling by `1 / length`: only `+ - * /` and `Sqrt`, and the number of tries
// depends on the stream alone.
Vec3 RandomUnitVec3()
{
    u64* state = RandomState();
    for (u32 attempt = 0; attempt < 32; ++attempt)
    {
        const Vec3 v(Unit(AdvanceRandom(state)) * 2.0f - 1.0f, Unit(AdvanceRandom(state)) * 2.0f - 1.0f,
                     Unit(AdvanceRandom(state)) * 2.0f - 1.0f);
        const f32 length_squared = v.Dot(v);
        if (length_squared > 1.0e-6f && length_squared <= 1.0f)
        {
            const f32 length = Sqrt(length_squared);
            return Vec3(v.x / length, v.y / length, v.z / length);
        }
    }
    return Vec3(0.0f, 0.0f, 1.0f);  // 32 misses in a row: (1 - pi/6)^32 of the streams, never a failure
}

void Emit(ScriptLogLevel level, StringView text)
{
    EmitScriptLog(level, text);  // a level the channel would drop is not buffered and does not use up the limit
}

void LogDebug(StringView text)
{
    Emit(ScriptLogLevel::Debug, text);
}

void LogInfo(StringView text)
{
    Emit(ScriptLogLevel::Info, text);
}

void LogWarn(StringView text)
{
    Emit(ScriptLogLevel::Warn, text);
}

void LogError(StringView text)
{
    Emit(ScriptLogLevel::Error, text);
}

f64 TimeDt()
{
    const PartitionContext* context = CurrentPartitionContext();
    return context != nullptr ? context->dt : 0.0;
}

f64 TimeNow()
{
    const PartitionContext* context = CurrentPartitionContext();
    return context != nullptr ? context->sim_time : 0.0;
}

i64 TimeFrame()
{
    const PartitionContext* context = CurrentPartitionContext();
    return context != nullptr ? static_cast<i64>(context->frame) : 0;
}

i64 TimeTick()
{
    const PartitionContext* context = CurrentPartitionContext();
    return context != nullptr ? static_cast<i64>(context->tick) : 0;
}

Value TimeReal()
{
    const PartitionContext* context = CurrentPartitionContext();
    if (context != nullptr && context->in_task)
    {
        RaiseScriptError("the wall clock is not available in a task: its result must depend only on its arguments");
        return Value();
    }
    if (context != nullptr && context->replay_recording)
    {
        // Refused: a wall-clock reading would make the recorded run unrepeatable.
        RaiseScriptError("the wall clock is refused while a replay is recording");
        return Value();
    }
    return Value::FromFloat(static_cast<f64>(os::WallClockUnixMillis()) / 1000.0);
}

}  // namespace

bool RegisterRuntimeBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&RandomFloat>(
        "random", "float", ThreadRule::Pure, "",
        "Returns the next number of the running entity's stream, from 0 up to but not "
        "including 1. The stream starts from the world seed, the entity's stable id and the "
        "tick, so it is the same for any thread count.");
    ok &= registry->Add<&RandomInt>("random", "int", ThreadRule::Pure, "lo, hi",
                                    "Returns an integer from `lo` to `hi`, both included (swapped when `lo` is above "
                                    "`hi`), from the running entity's stream.");
    ok &= registry->Add<&RandomUnitVec3>(
        "random", "vec3_unit", ThreadRule::Pure, "",
        "Returns a vector of length 1 pointing in a random direction, from the running "
        "entity's stream.");
    ok &= registry->Add<&LogDebug>("log", "debug", ThreadRule::Pure, "text",
                                   "Writes `text` to the `LogScript` channel at debug level, tagged with the script "
                                   "and line. Buffered per partition and flushed in partition order at the sync point; "
                                   "a script that writes more than the per-tick limit (`script.log_limit`) loses the "
                                   "rest, counted in one warning.");
    ok &= registry->Add<&LogInfo>("log", "info", ThreadRule::Pure, "text",
                                  "Writes `text` to the `LogScript` channel at info level (buffered, rate limited as "
                                  "`log.debug`).");
    ok &= registry->Add<&LogWarn>(
        "log", "warn", ThreadRule::Pure, "text",
        "Writes `text` to the `LogScript` channel at warning level (buffered, rate limited as "
        "`log.debug`).");
    ok &= registry->Add<&LogError>("log", "error", ThreadRule::Pure, "text",
                                   "Writes `text` to the `LogScript` channel at error level (buffered, rate limited as "
                                   "`log.debug`).");
    ok &= registry->Add<&TimeDt>("time", "dt", ThreadRule::Pure, "",
                                 "Returns the simulation seconds one tick advances (0 outside a tick).");
    ok &= registry->Add<&TimeNow>("time", "now", ThreadRule::Pure, "",
                                  "Returns the simulation clock in seconds at the tick start; deterministic, the same "
                                  "for every script in the tick.");
    ok &= registry->Add<&TimeFrame>("time", "frame", ThreadRule::Pure, "", "Returns the frame number.");
    ok &= registry->Add<&TimeTick>("time", "tick", ThreadRule::Pure, "", "Returns the tick number.");
    ok &= registry->Add<&TimeReal>("time", "real", ThreadRule::Pure, "",
                                   "Returns the wall clock in seconds since 1970. NOT deterministic: nothing that "
                                   "reaches the world should depend on it. Raises an error while a replay is "
                                   "being recorded.");
    return ok;
}

}  // namespace vse::script

#include "script/script_persist.h"

#include "script/script_runtime.h"
#include "storage/status.h"

namespace vse::script
{

namespace
{

storage::Status Corrupt(const char* what)
{
    return storage::Status::Make(storage::ErrorCode::Corrupt, what);
}

ValueLimits RecordLimits(const storage::CodecLimits& limits)
{
    ValueLimits v;
    v.max_text_bytes = static_cast<usize>(limits.max_string_bytes);
    v.max_blob_bytes = static_cast<usize>(limits.max_record_bytes);
    return v;
}

storage::Status SaveScript(const void* component, bytes::ByteWriter& out, const storage::CodecLimits& limits)
{
    const ScriptComponent& c = *static_cast<const ScriptComponent*>(component);
    if (c.behavior.View().Length() > kMaxBehaviorNameBytes)
    {
        return storage::Status::Make(storage::ErrorCode::LimitExceeded, "a behavior name is too long to save");
    }
    out.WriteVarint(c.behavior.View().Length());
    out.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(c.behavior.View().Data()), c.behavior.View().Length()));
    out.WriteBool(c.enabled);
    EncodeValueRecord(c.params, &out, nullptr);
    if (out.Size() > limits.max_record_bytes)
    {
        return storage::Status::Make(storage::ErrorCode::LimitExceeded, "a script record is too large");
    }
    return storage::Status::Success();
}

storage::Status LoadScript(bytes::ByteReader& in, void* component, const storage::CodecLimits& limits)
{
    ScriptComponent& c = *static_cast<ScriptComponent*>(component);
    u64 length = 0;
    if (in.ReadLength(kMaxBehaviorNameBytes, &length) != bytes::ByteError::Ok)
    {
        return Corrupt("a script record has a bad behavior name length");
    }
    ByteSpan name;
    if (in.ReadBytes(static_cast<usize>(length), &name) != bytes::ByteError::Ok)
    {
        return Corrupt("a script record is cut short in the behavior name");
    }
    bool enabled = true;
    if (in.ReadBool(&enabled) != bytes::ByteError::Ok)
    {
        return Corrupt("a script record has a bad enabled flag");
    }
    Value params;
    const ValueRecordStatus s = DecodeValueRecord(&in, &params, nullptr, RecordLimits(limits));
    if (s != ValueRecordStatus::Ok)
    {
        return Corrupt("a script record has parameters that do not decode");
    }
    if (params.Kind() != ValueKind::Nil && params.Kind() != ValueKind::Map)
    {
        return Corrupt("a script record's parameters are neither nil nor a map");
    }
    c.behavior = String<HeapAllocator>(HeapAllocator::Default(),
                                       StringView(reinterpret_cast<const char*>(name.Data()), name.Size()));
    c.params = Move(params);
    c.enabled = enabled;
    return storage::Status::Success();
}

storage::Status SavePersist(const void* component, bytes::ByteWriter& out, const storage::CodecLimits& limits)
{
    const ScriptPersist& c = *static_cast<const ScriptPersist*>(component);
    if (c.blob.Size() > kMaxPersistBytes || c.blob.Size() > limits.max_record_bytes)
    {
        return storage::Status::Make(storage::ErrorCode::LimitExceeded, "a persistent record is too large to save");
    }
    out.WriteVarint(c.blob.Size());
    out.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(c.blob.Data()), c.blob.Size()));
    return storage::Status::Success();
}

storage::Status LoadPersist(bytes::ByteReader& in, void* component, const storage::CodecLimits& limits)
{
    ScriptPersist& c = *static_cast<ScriptPersist*>(component);
    u64 length = 0;
    const u64 cap = limits.max_record_bytes < kMaxPersistBytes ? limits.max_record_bytes : kMaxPersistBytes;
    if (in.ReadLength(cap, &length) != bytes::ByteError::Ok)
    {
        return Corrupt("a persistent record has a bad length");
    }
    ByteSpan data;
    if (in.ReadBytes(static_cast<usize>(length), &data) != bytes::ByteError::Ok)
    {
        return Corrupt("a persistent record is cut short");
    }
    // The bytes must be a value record that is a map; entity keys are read as no entity here (the check is of shape).
    bytes::ByteReader check(data);
    Value value;
    if (DecodeValueRecord(&check, &value, nullptr, RecordLimits(limits)) != ValueRecordStatus::Ok || !check.AtEnd() ||
        value.Kind() != ValueKind::Map)
    {
        return Corrupt("a persistent record is not a map of values");
    }
    c.blob.Clear();
    for (usize i = 0; i < data.Size(); ++i)
    {
        c.blob.Push(static_cast<u8>(data[i]));
    }
    return storage::Status::Success();
}

}  // namespace

bool RegisterScriptComponents(storage::ComponentRegistry* registry)
{
    storage::ComponentDesc script;
    script.id = kScriptComponentId;
    script.name = "script";
    script.custom.save = &SaveScript;
    script.custom.load = &LoadScript;
    storage::ComponentDesc persist;
    persist.id = kScriptPersistId;
    persist.name = "script_persist";
    persist.custom.save = &SavePersist;
    persist.custom.load = &LoadPersist;
    bool ok = registry->Register<ScriptComponent>(script).Ok();
    ok &= registry->Register<ScriptPersist>(persist).Ok();
    return ok;
}

}  // namespace vse::script

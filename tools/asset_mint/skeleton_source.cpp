#include "skeleton_source.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "lang/scalar.h"

namespace engine_placeholder::asset
{
namespace
{

constexpr f64 kSourceUnitTolerance = 1e-2;

struct SrcLineTokens
{
    StringView tokens[24];
    usize count = 0;
    bool overflow = false;
};

bool SrcIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

SrcLineTokens SrcTokenize(StringView line)
{
    SrcLineTokens out;
    usize i = 0;
    while (i < line.Length())
    {
        while (i < line.Length() && SrcIsSpace(line[i]))
        {
            ++i;
        }
        if (i >= line.Length() || line[i] == '#')
        {
            break;
        }
        const usize start = i;
        while (i < line.Length() && !SrcIsSpace(line[i]) && line[i] != '#')
        {
            ++i;
        }
        if (out.count == 24)
        {
            out.overflow = true;
            break;
        }
        out.tokens[out.count++] = line.Substring(start, i - start);
    }
    return out;
}

bool SrcNumber(StringView token, f32* out)
{
    const ParseDoubleResult r = ParseDoubleExact(token.Data(), token.Data() + token.Length());
    if (!r.ok || r.end != token.Data() + token.Length() || !IsFinite(r.value) || Abs(r.value) > 3.0e38)
    {
        return false;
    }
    *out = static_cast<f32>(r.value);
    return true;
}

void SrcSetError(SkeletonSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status SrcFail(SkeletonSourceError* error, u32 line, const char* message)
{
    SrcSetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the skeleton source is invalid");
}

bool SrcFindJoint(const Vector<SkeletonJoint>& joints, StringView name, u32* index)
{
    for (usize i = 0; i < joints.Size(); ++i)
    {
        if (StringView(joints[i].name.Data(), CStrLength(joints[i].name.Data())) == name)
        {
            *index = static_cast<u32>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

Status ParseSkeletonSource(StringView text, SkeletonSource* out, SkeletonSourceError* error)
{
    *out = SkeletonSource();
    *error = SkeletonSourceError();
    bool have_id = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const SrcLineTokens t = SrcTokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return SrcFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return SrcFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return SrcFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
        }
        else if (kind == StringView("joint"))
        {
            if (!have_id)
            {
                return SrcFail(error, line_number, "the id line must come first");
            }
            if (t.count != 13)
            {
                return SrcFail(error, line_number, "joint needs NAME PARENT and 10 numbers");
            }
            if (out->joints.Size() >= kMaxSkeletonJoints)
            {
                SrcSetError(error, line_number, "more than 256 joints");
                return Status::Make(ErrorCode::LimitExceeded, "a skeleton holds at most 256 joints in this version");
            }
            SkeletonJoint joint;
            MemSet(&joint, 0, sizeof(joint));
            const StringView name = t.tokens[1];
            if (name.Length() > kMaxJointNameBytes)
            {
                return SrcFail(error, line_number, "a joint name is longer than 64 bytes");
            }
            u32 index = 0;
            if (SrcFindJoint(out->joints, name, &index))
            {
                return SrcFail(error, line_number, "duplicate joint name");
            }
            MemCopy(joint.name.Data(), name.Data(), name.Length());
            const StringView parent = t.tokens[2];
            if (parent == StringView("-"))
            {
                joint.parent = kNoParent;
            }
            else if (SrcFindJoint(out->joints, parent, &index))
            {
                joint.parent = static_cast<u16>(index);
            }
            else if (parent == name)
            {
                return SrcFail(error, line_number, "a joint cannot be its own parent");
            }
            else
            {
                return SrcFail(error, line_number, "unknown parent (a parent must be defined before its children)");
            }
            f32 v[10];
            for (u32 k = 0; k < 10; ++k)
            {
                if (!SrcNumber(t.tokens[3 + k], &v[k]))
                {
                    return SrcFail(error, line_number, "a number is malformed or not finite");
                }
            }
            f64 length_squared = 0;
            for (u32 k = 3; k < 7; ++k)
            {
                length_squared += static_cast<f64>(v[k]) * v[k];
            }
            const f64 length = Sqrt(length_squared);
            if (Abs(length - 1.0) > kSourceUnitTolerance)
            {
                return SrcFail(error, line_number, "the rotation is not a unit quaternion");
            }
            if (v[7] == 0.0f || v[8] == 0.0f || v[9] == 0.0f)
            {
                return SrcFail(error, line_number, "a scale is zero");
            }
            const f64 sign = v[6] < 0.0f ? -1.0 : 1.0;  // the hemisphere rule: w >= 0
            for (u32 k = 0; k < 3; ++k)
            {
                joint.translation[k] = v[k];
                joint.scale[k] = v[7 + k];
            }
            for (u32 k = 0; k < 4; ++k)
            {
                const f64 q = sign * static_cast<f64>(v[3 + k]) / length;
                joint.rotation[k] = static_cast<f32>(q == 0.0 ? 0.0 : q);  // no negative zero
            }
            out->joints.Push(joint);
            out->has_ibm.Push(false);
        }
        else if (kind == StringView("ibm"))
        {
            if (t.count != 14)
            {
                return SrcFail(error, line_number, "ibm needs NAME and 12 numbers");
            }
            u32 index = 0;
            if (!SrcFindJoint(out->joints, t.tokens[1], &index))
            {
                return SrcFail(error, line_number, "ibm names an unknown joint");
            }
            if (out->has_ibm[index])
            {
                return SrcFail(error, line_number, "the joint's ibm is given twice");
            }
            for (u32 k = 0; k < 12; ++k)
            {
                if (!SrcNumber(t.tokens[2 + k], &out->joints[index].inverse_bind[k]))
                {
                    return SrcFail(error, line_number, "a number is malformed or not finite");
                }
            }
            out->has_ibm[index] = true;
        }
        else
        {
            return SrcFail(error, line_number, "unknown record (expected id, joint or ibm)");
        }
    }
    if (!have_id)
    {
        return SrcFail(error, 0, "the source has no id line");
    }
    if (out->joints.Size() == 0)
    {
        return SrcFail(error, 0, "the source has no joints");
    }
    return Status::Success();
}

Status MintSkeleton(StringView text, const TypeRegistry& types, Vector<byte>* out, SkeletonSourceError* error,
                    u32 target)
{
    SkeletonSource source;
    ENGINE_TRY(ParseSkeletonSource(text, &source, error));
    // Computed matrices for every joint, then the source's overrides put back.
    Vector<SkeletonJoint> computed;
    for (usize i = 0; i < source.joints.Size(); ++i)
    {
        computed.Push(source.joints[i]);
    }
    ComputeInverseBindMatrices(Span<SkeletonJoint>(computed.Data(), computed.Size()));
    for (usize i = 0; i < source.joints.Size(); ++i)
    {
        if (!source.has_ibm[i])
        {
            source.joints[i].inverse_bind = computed[i].inverse_bind;
        }
    }
    out->Clear();
    const Status built = BuildSkeletonBlob(
        types, source.id, Span<const SkeletonJoint>(source.joints.Data(), source.joints.Size()), true, out, target);
    if (built.Failed())
    {
        SrcSetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace engine_placeholder::asset

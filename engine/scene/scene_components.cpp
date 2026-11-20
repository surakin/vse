#include "scene/scene_components.h"

#include "scene/components.h"
#include "storage/field.h"

namespace vse::scene
{

namespace
{

// light_source version 1 had no cone: the record is copied and the version-2 defaults follow (glTF coverage Day 11).
storage::Status LightSourceV1ToV2(bytes::ByteReader& in, bytes::ByteWriter& out, const storage::CodecLimits&)
{
    while (in.Remaining() > 0)
    {
        u8 b = 0;
        const bytes::ByteError e = in.ReadU8(&b);
        if (e != bytes::ByteError::Ok)
        {
            return storage::detail::FromByteError(e);
        }
        out.WriteU8(b);
    }
    out.WriteF32(0.0f);           // inner_cone
    out.WriteF32(0.7853981634f);  // outer_cone: pi/4, glTF's default
    return storage::Status::Success();
}

}  // namespace

storage::Status RegisterSceneComponents(storage::ComponentRegistry* registry)
{
    using storage::Field;
    using storage::FieldOptions;
    storage::Status s = registry->Register<LightSource>(
        {.id = kLightSourceId,
         .name = "light_source",
         .version = 2,
         .fields = {Field<&LightSource::type>("type", FieldOptions::Enum(0, 3)), Field<&LightSource::color>("color"),
                    Field<&LightSource::intensity>("intensity"), Field<&LightSource::radius>("radius"),
                    Field<&LightSource::direction>("direction"), Field<&LightSource::inner_cone>("inner_cone"),
                    Field<&LightSource::outer_cone>("outer_cone")},
         .upgrades = {storage::UpgradeStep{1, &LightSourceV1ToV2}}});
    if (s.Failed())
    {
        return s;
    }
    s = registry->Register<Camera>(
        {.id = kCameraId,
         .name = "camera",
         .fields = {Field<&Camera::fov_y>("fov_y"), Field<&Camera::aspect>("aspect"),
                    Field<&Camera::near_plane>("near_plane"), Field<&Camera::far_plane>("far_plane")}});
    if (s.Failed())
    {
        return s;
    }
    return registry->Register<Hidden>({.id = kHiddenId, .name = "hidden"});
}

}  // namespace vse::scene

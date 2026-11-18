#include "scene/scene_components.h"

#include "scene/components.h"
#include "storage/field.h"

namespace engine_placeholder::scene
{

storage::Status RegisterSceneComponents(storage::ComponentRegistry* registry)
{
    using storage::Field;
    using storage::FieldOptions;
    storage::Status s = registry->Register<LightSource>(
        {.id = kLightSourceId,
         .name = "light_source",
         .fields = {Field<&LightSource::type>("type", FieldOptions::Enum(0, 2)), Field<&LightSource::color>("color"),
                    Field<&LightSource::intensity>("intensity"), Field<&LightSource::radius>("radius"),
                    Field<&LightSource::direction>("direction")}});
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

}  // namespace engine_placeholder::scene

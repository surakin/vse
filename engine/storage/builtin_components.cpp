#include "storage/builtin_components.h"

#include "ecs/transform.h"
#include "spatial/components.h"

namespace engine_placeholder::storage {

Status RegisterBuiltinComponents(ComponentRegistry* registry) {
    Status s = registry->Register<ecs::LocalTransform>({.id = kLocalTransformId,
                                                        .name = "local_transform",
                                                        .fields = {Field<&ecs::LocalTransform::position>("position"),
                                                                   Field<&ecs::LocalTransform::rotation>("rotation"),
                                                                   Field<&ecs::LocalTransform::scale>("scale")}});
    if (s.Ok()) s = registry->Register<ecs::WorldTransform>({.id = kWorldTransformId, .name = "world_transform", .flags = kComponentDerived});
    if (s.Ok()) s = registry->Register<ecs::Parent>({.id = kParentId, .name = "parent", .flags = kComponentSkip});
    if (s.Ok()) s = registry->Register<ecs::HierarchyDepth>({.id = kHierarchyDepthId, .name = "hierarchy_depth", .flags = kComponentDerived});
    if (s.Ok()) {
        s = registry->Register<spatial::SpatialBounds>({.id = kSpatialBoundsId, .name = "spatial_bounds", .fields = {Field<&spatial::SpatialBounds::local>("local")}});
    }
    if (s.Ok()) {
        s = registry->Register<spatial::SpatialMembership>({.id = kSpatialMembershipId,
                                                            .name = "spatial_membership",
                                                            .fields = {Field<&spatial::SpatialMembership::sets>("sets"),
                                                                       Field<&spatial::SpatialMembership::layer>("layer")}});
    }
    if (s.Ok()) s = registry->Register<spatial::SpatialStatic>({.id = kSpatialStaticId, .name = "spatial_static"});
    if (s.Ok()) s = registry->Register<spatial::WorldBounds>({.id = kWorldBoundsId, .name = "world_bounds", .flags = kComponentDerived});
    if (s.Ok()) s = registry->Register<spatial::SpatialProxy>({.id = kSpatialProxyId, .name = "spatial_proxy", .flags = kComponentDerived});
    return s;
}

} // namespace engine_placeholder::storage

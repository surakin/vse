#pragma once

//! The scene's components in the storage registry (Phase 27 Day 456): [[scene::LightSource]], [[scene::Camera]] and
//! [[scene::Hidden]], so that levels, saves and scripts (`component.add(e, "light_source", {...})`) can name them.
//! [[scene::MeshRenderer]] holds a shared GPU mesh, which has no stored form; a mesh is named by its asset id and built
//! by the level spawner or, for scripts, by the resource bindings (Phase 27 Day 463).

#include "storage/component_registry.h"

namespace engine_placeholder::scene
{

/// The stable ids of the scene's components (never change or reuse one).
enum SceneComponentId : u32
{
    kLightSourceId = 60,  ///< [[scene::LightSource]]: stored.
    kCameraId = 61,       ///< [[scene::Camera]]: stored.
    kHiddenId = 62,       ///< [[scene::Hidden]]: a stored tag.
};

/// Registers `light_source`, `camera` and `hidden` in `registry`.
///
/// - Param registry: the registry to fill; not null.
/// - Ownership: borrowed for the call.
/// - Fails: `Exists` when an id, a name or a type is registered already (the registry then holds the ones before it).
storage::Status RegisterSceneComponents(storage::ComponentRegistry* registry);

}  // namespace engine_placeholder::scene

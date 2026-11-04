#pragma once

//! Identification of the renderer library; the first real symbol of Phase 4 (the camera and scene arrive on Day 73).

namespace engine_placeholder::renderer {

/// Returns a short static description of the renderer, for logs and tools.
///
/// Returns: a non-null string with static storage duration.
/// Thread-safety: thread-safe.
/// Ownership: the string is static and never freed by the caller.
const char* RendererName();

} // namespace engine_placeholder::renderer

// Linux has no policy layer yet (Phase 13 Day 157): an administrator's enforced values would be a read-only file under /etc, not designed yet.
#include "settings/policy_layers.h"

namespace engine_placeholder::settings::detail {

void AddPolicyLayers(const LoadOptions&, LayerStack*) {}

} // namespace engine_placeholder::settings::detail

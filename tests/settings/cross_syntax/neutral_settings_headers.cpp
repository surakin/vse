// Every backend-neutral settings header, included in one translation unit for the cross-target syntax check (Phase 11
// Day 139). Nothing here may depend on POSIX or on a Linux-only facility: the Windows and macOS stores implement
// ConfigStore and everything else is shared.
#include "settings/config_store.h"
#include "settings/config_text.h"
#include "settings/desc.h"
#include "settings/engine_settings.h"
#include "settings/env_source.h"
#include "settings/file_store.h"
#include "settings/key.h"
#include "settings/layers.h"
#include "settings/memory_store.h"
#include "settings/migration.h"
#include "settings/registry.h"
#include "settings/report.h"
#include "settings/setting.h"
#include "settings/settings.h"
#include "settings/value.h"

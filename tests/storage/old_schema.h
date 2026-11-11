#pragma once

// The first schema of the SQL store (Phase 19 Day 262), kept so that the upgrade test builds a real old database:
// schema version 1, records without `seq`.

#include "database/database.h"
#include "storage/store.h"

namespace engine_placeholder::storage::test
{

// Creates the version-1 tables in `connection`, stores `records` in them and the metadata `("format_version", "1")`,
// and sets the schema version to 1. Written independently of SqlStore: the key and hash encodings are spelled out again
// here on purpose.
database::Status BuildSchemaOne(database::Connection& connection, const Vector<EntityRecord>& records);

}  // namespace engine_placeholder::storage::test

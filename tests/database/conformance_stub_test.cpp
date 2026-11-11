// Instantiates the conformance suite (Phase 19 Day 252) for backends that do not exist yet; every case skips until the backend lands (Day 253: memory,
// Day 255: SQLite).

#include "conformance.h"

namespace engine_placeholder::database {

struct UnimplementedFixture {
    static constexpr bool kImplemented = false;
    static const char* Name() { return "unimplemented"; }
    Database& Db() { return *db_; }
    OpenOptions Options() const { return OpenOptions(); }

private:
    Database* db_ = nullptr;
};

DATABASE_CONFORMANCE_CASES(UnimplementedFixture)

} // namespace engine_placeholder::database

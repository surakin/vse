// Instantiates the conformance suite (Phase 19 Day 252) for a backend that does not exist; every case skips. It proves
// the suite compiles and skips cleanly; the real backends instantiate it in their own test files.

#include "conformance.h"

namespace engine_placeholder::database
{

struct UnimplementedFixture
{
    static constexpr bool kImplemented = false;
    static constexpr bool kSharedLocation = true;
    static const char* Name() { return "unimplemented"; }
    Database& Db() { return *db_; }
    OpenOptions Options() const { return OpenOptions(); }

private:
    Database* db_ = nullptr;
};

DATABASE_CONFORMANCE_CASES(UnimplementedFixture)

}  // namespace engine_placeholder::database

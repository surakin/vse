#include <catch2/catch_test_macros.hpp>

#include "debug/crash_handler.h"

#include "lang/traits.h"
#include "lang/utility.h"

using namespace engine_placeholder::debug;
using engine_placeholder::Declval;
using engine_placeholder::FalseType;
using engine_placeholder::TrueType;
using engine_placeholder::VoidType;

// The options struct has no way to say where to send a dump: no address, no account, no transport, no upload switch.
// Each line fails to compile if such a field is ever added under one of these names.
namespace {

template <typename T, typename = void>
struct HasDsn : FalseType {};
template <typename T>
struct HasDsn<T, VoidType<decltype(Declval<T&>().dsn)>> : TrueType {};
template <typename T, typename = void>
struct HasUrl : FalseType {};
template <typename T>
struct HasUrl<T, VoidType<decltype(Declval<T&>().url)>> : TrueType {};
template <typename T, typename = void>
struct HasEndpoint : FalseType {};
template <typename T>
struct HasEndpoint<T, VoidType<decltype(Declval<T&>().endpoint)>> : TrueType {};
template <typename T, typename = void>
struct HasTransport : FalseType {};
template <typename T>
struct HasTransport<T, VoidType<decltype(Declval<T&>().transport)>> : TrueType {};
template <typename T, typename = void>
struct HasUpload : FalseType {};
template <typename T>
struct HasUpload<T, VoidType<decltype(Declval<T&>().upload)>> : TrueType {};

static_assert(!HasDsn<CrashHandlerOptions>::value, "crash handler options must not carry a DSN");
static_assert(!HasUrl<CrashHandlerOptions>::value, "crash handler options must not carry a URL");
static_assert(!HasEndpoint<CrashHandlerOptions>::value, "crash handler options must not carry an endpoint");
static_assert(!HasTransport<CrashHandlerOptions>::value, "crash handler options must not carry a transport");
static_assert(!HasUpload<CrashHandlerOptions>::value, "crash handler options must not carry an upload switch");

} // namespace

TEST_CASE("The crash handler options have no field that could name a destination", "[crash][debug]") {
    CrashHandlerOptions options; // the static_asserts above are the test; this keeps it from being an empty test case
    CHECK(options.dump_directory == nullptr);
    CHECK(options.report_file == nullptr);
    CHECK(options.release == nullptr);
    CHECK(options.handler_path == nullptr);
    CHECK(options.enabled);
}

TEST_CASE("A disabled crash handler is not installed and changes nothing", "[crash][debug]") {
    CHECK_FALSE(CrashHandlerInstalled());
    CrashHandlerOptions options;
    options.enabled = false;
    CHECK(InstallCrashHandler(options) == CrashHandlerResult::Disabled);
    CHECK_FALSE(CrashHandlerInstalled());
    UninstallCrashHandler(); // nothing installed: does nothing
    CHECK_FALSE(CrashHandlerInstalled());
}

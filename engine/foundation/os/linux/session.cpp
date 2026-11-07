#include "os/session.h"

#include "os/os.h"

// The session manager on Linux is systemd-logind. Its answer is read over D-Bus with sd-bus from libsystemd, loaded at run time: the engine
// The query is about the calling process's own logind session ("auto"): a run over ssh against the physical desktop's display reports the ssh session, not the desktop's.
// The engine neither includes systemd headers nor links the library, and a system without it (a container, Alpine) answers Unknown.

namespace engine_placeholder::os {
namespace {

struct SdBus;
struct SdBusError {
    const char* name;
    const char* message;
    int need_free;
};

using OpenSystemFn = int (*)(SdBus**);
using GetPropertyFn = int (*)(SdBus*, const char*, const char*, const char*, const char*, SdBusError*, char, void*);
using ErrorFreeFn = void (*)(SdBusError*);
using CloseFn = SdBus* (*)(SdBus*);
using SetTimeoutFn = int (*)(SdBus*, unsigned long long);

constexpr unsigned long long kCallTimeoutMicros = 1'000'000; // a logind that does not answer within a second gives Unknown, so a query costs 3 seconds at worst

constexpr const char* kService = "org.freedesktop.login1";
constexpr const char* kSession = "/org/freedesktop/login1/session/auto"; // logind resolves "auto" to the caller's session
constexpr const char* kInterface = "org.freedesktop.login1.Session";

bool ReadBool(GetPropertyFn get, ErrorFreeFn free_error, SdBus* bus, const char* member, bool* out) {
    SdBusError error = {nullptr, nullptr, 0};
    int value = 0;
    const int r = get(bus, kService, kSession, kInterface, member, &error, 'b', &value);
    free_error(&error);
    if (r < 0) return false;
    *out = value != 0;
    return true;
}

} // namespace

SessionState QuerySessionState() {
    // The library stays loaded once found (unloading it after every query only costs time; the handle is deliberately never released).
    static const DynamicLibraryHandle lib = LoadLibrary("libsystemd.so.0");
    if (lib == nullptr) return SessionState::Unknown;
    auto open_system = reinterpret_cast<OpenSystemFn>(GetSymbol(lib, "sd_bus_open_system"));
    auto get_property = reinterpret_cast<GetPropertyFn>(GetSymbol(lib, "sd_bus_get_property_trivial"));
    auto free_error = reinterpret_cast<ErrorFreeFn>(GetSymbol(lib, "sd_bus_error_free"));
    auto close = reinterpret_cast<CloseFn>(GetSymbol(lib, "sd_bus_flush_close_unref"));
    auto set_timeout = reinterpret_cast<SetTimeoutFn>(GetSymbol(lib, "sd_bus_set_method_call_timeout"));
    SessionState state = SessionState::Unknown;
    if (open_system != nullptr && get_property != nullptr && free_error != nullptr && close != nullptr) {
        SdBus* bus = nullptr;
        if (open_system(&bus) >= 0 && bus != nullptr) {
            if (set_timeout != nullptr) set_timeout(bus, kCallTimeoutMicros);
            bool active = false, locked = false, idle = false;
            const bool known = ReadBool(get_property, free_error, bus, "Active", &active) && ReadBool(get_property, free_error, bus, "LockedHint", &locked) &&
                               ReadBool(get_property, free_error, bus, "IdleHint", &idle);
            state = SessionStateFromFlags(known, active, locked, idle);
            close(bus);
        }
    }
    return state;
}

} // namespace engine_placeholder::os

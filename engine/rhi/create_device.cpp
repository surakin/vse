#include "cstr/format.h"
#include "cstr/cstr.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/backend_registry.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi {

namespace {

struct Registry {
    Mutex mu;
    BackendDescriptor items[kMaxRegisteredBackends];
    usize count = 0;
};

Registry& TheRegistry() {
    static Registry registry;
    return registry;
}

void SetReason(char* reason, usize reason_size, const char* format, const char* a, const char* b) {
    if (reason != nullptr && reason_size != 0) {
        FormatTo(reason, reason_size, format, a, b);
    }
}

const char* RequestedName(Backend backend) {
    switch (backend) {
    case Backend::Null: return "null";
    case Backend::Vulkan: return "vulkan";
    case Backend::Dx12: return "dx12";
    case Backend::Auto: return "auto";
    }
    return "unknown";
}

bool Same(const BackendDescriptor& a, const BackendDescriptor& b) {
    return a.backend == b.backend && CStrCompare(a.name, b.name) == 0 && a.create == b.create &&
           a.enumerate_adapters == b.enumerate_adapters && a.available == b.available && a.shader_format == b.shader_format &&
           a.auto_rank == b.auto_rank;
}

} // namespace

bool RegisterBackend(const BackendDescriptor& descriptor) {
    if (descriptor.name == nullptr || descriptor.name[0] == '\0' || descriptor.create == nullptr ||
        descriptor.enumerate_adapters == nullptr || descriptor.backend == Backend::Auto) {
        RhiLog(LogVerbosity::Error, "RegisterBackend: the descriptor needs a name, a create and an enumerate_adapters function, and a backend other than auto");
        return false;
    }
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    for (usize i = 0; i < r.count; ++i) {
        const BackendDescriptor& e = r.items[i];
        if (Same(e, descriptor)) {
            return true;
        }
        if (e.backend == descriptor.backend || CStrCompare(e.name, descriptor.name) == 0) {
            RhiLog(LogVerbosity::Error, "RegisterBackend: '%s' clashes with the registered backend '%s'", descriptor.name, e.name);
            return false;
        }
    }
    if (r.count == kMaxRegisteredBackends) {
        RhiLog(LogVerbosity::Error, "RegisterBackend: the table is full (%zu backends); '%s' is not registered", kMaxRegisteredBackends,
               descriptor.name);
        return false;
    }
    r.items[r.count++] = descriptor;
    return true;
}

const BackendDescriptor* FindBackend(Backend backend) {
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    for (usize i = 0; i < r.count; ++i) {
        if (r.items[i].backend == backend) {
            return &r.items[i];
        }
    }
    return nullptr;
}

const BackendDescriptor* FindBackendByName(StringView name) {
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    for (usize i = 0; i < r.count; ++i) {
        if (StringView(r.items[i].name) == name) {
            return &r.items[i];
        }
    }
    return nullptr;
}

usize RegisteredBackendCount() {
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    return r.count;
}

const BackendDescriptor& RegisteredBackend(usize index) {
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    ENGINE_ASSERT(index < r.count, "RegisteredBackend: the index is out of range");
    return r.items[index];
}

bool BackendAvailable(const BackendDescriptor& descriptor, char* reason, usize reason_size) {
    if (reason != nullptr && reason_size != 0) {
        reason[0] = '\0';
    }
    if (descriptor.available == nullptr) {
        return true;
    }
    char local[160] = {};
    char* out = reason != nullptr && reason_size != 0 ? reason : local;
    const usize out_size = reason != nullptr && reason_size != 0 ? reason_size : sizeof(local);
    return descriptor.available(out, out_size);
}

const BackendDescriptor* PickAutoBackend(char* reason, usize reason_size) {
    // Copy the table under the lock, then probe outside it: a probe may load a library.
    BackendDescriptor items[kMaxRegisteredBackends];
    const BackendDescriptor* addresses[kMaxRegisteredBackends] = {};
    usize count = 0;
    {
        Registry& r = TheRegistry();
        LockGuard<Mutex> lock(r.mu);
        count = r.count;
        for (usize i = 0; i < count; ++i) {
            items[i] = r.items[i];
            addresses[i] = &r.items[i];
        }
    }
    const BackendDescriptor* best = nullptr;
    u32 best_rank = 0;
    char last_reason[256] = "no backend that auto may pick is registered in this program"; // fits the name and a 160-byte reason
    for (usize i = 0; i < count; ++i) {
        if (items[i].auto_rank == 0 || items[i].auto_rank <= best_rank) {
            continue;
        }
        char why[160] = {};
        if (!BackendAvailable(items[i], why, sizeof(why))) {
            FormatTo(last_reason, sizeof(last_reason), "%s is not available: %s", items[i].name, why);
            continue;
        }
        best = addresses[i];
        best_rank = items[i].auto_rank;
    }
    if (best == nullptr) {
        SetReason(reason, reason_size, "%s%s", last_reason, "");
    } else if (reason != nullptr && reason_size != 0) {
        reason[0] = '\0';
    }
    return best;
}

const BackendDescriptor* ResolveBackend(Backend backend, char* reason, usize reason_size) {
    if (backend == Backend::Auto) {
        return PickAutoBackend(reason, reason_size);
    }
    const BackendDescriptor* d = FindBackend(backend);
    if (d == nullptr) {
        SetReason(reason, reason_size,
                  "the %s backend is not registered in this program (link rhi_backends and call rhi::RegisterCompiledBackends%s)",
                  RequestedName(backend), " before creating a device");
        return nullptr;
    }
    char why[160] = {};
    if (!BackendAvailable(*d, why, sizeof(why))) {
        SetReason(reason, reason_size, "the %s backend is not available here: %s", d->name, why);
        return nullptr;
    }
    if (reason != nullptr && reason_size != 0) {
        reason[0] = '\0';
    }
    return d;
}

void ClearBackendRegistryForTest() {
    Registry& r = TheRegistry();
    LockGuard<Mutex> lock(r.mu);
    r.count = 0;
}

DevicePtr CreateDevice(const DeviceCreateInfo& info) {
    char reason[256] = {};
    const BackendDescriptor* d = ResolveBackend(info.backend, reason, sizeof(reason));
    if (d == nullptr) {
        RhiLog(LogVerbosity::Error, "CreateDevice: %s", reason);
        return DevicePtr();
    }
    if (info.backend == d->backend) {
        return d->create(info);
    }
    DeviceCreateInfo resolved = info;
    resolved.backend = d->backend;
    return d->create(resolved);
}

bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out) {
    *out = AdapterList{};
    char reason[256] = {};
    const BackendDescriptor* d = ResolveBackend(info.backend, reason, sizeof(reason));
    if (d == nullptr) {
        RhiLog(LogVerbosity::Error, "EnumerateAdapters: %s", reason);
        return false;
    }
    if (info.backend == d->backend) {
        return d->enumerate_adapters(info, out);
    }
    DeviceCreateInfo resolved = info;
    resolved.backend = d->backend;
    return d->enumerate_adapters(resolved, out);
}

} // namespace engine_placeholder::rhi

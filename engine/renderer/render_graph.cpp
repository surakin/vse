#include "renderer/render_graph.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "jobs/parallel_for.h"
#include "log/assert.h"
#include "renderer/transient_pool.h"

namespace engine_placeholder::renderer {

namespace {

constexpr u32 kNone = 0xffffffffu;

enum EdgeKind : u8 { kRaw = 0, kWaw, kWar, kAfter };

const char* EdgeName(u8 kind) {
    switch (kind) {
    case kRaw: return "raw";
    case kWaw: return "waw";
    case kWar: return "war";
    default: return "after";
    }
}

const char* AccessName(GraphAccess a) {
    switch (a) {
    case GraphAccess::Read: return "read";
    case GraphAccess::Write: return "write";
    default: return "readwrite";
    }
}

bool TextureUsageOk(GraphUsage u) {
    switch (u) {
    case GraphUsage::ColorAttachment:
    case GraphUsage::DepthAttachment:
    case GraphUsage::DepthRead:
    case GraphUsage::Sampled:
    case GraphUsage::StorageRead:
    case GraphUsage::StorageWrite:
    case GraphUsage::CopySrc:
    case GraphUsage::CopyDst:
        return true;
    default:
        return false;
    }
}

bool BufferUsageOk(GraphUsage u) {
    switch (u) {
    case GraphUsage::UniformBuffer:
    case GraphUsage::VertexBuffer:
    case GraphUsage::IndexBuffer:
    case GraphUsage::StorageRead:
    case GraphUsage::StorageWrite:
    case GraphUsage::CopySrc:
    case GraphUsage::CopyDst:
        return true;
    default:
        return false;
    }
}

// Bounded text output: counts the full length, stores what fits.
struct RenderGraphWriter {
    char* out;
    usize capacity;
    usize length = 0;

    void Add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        const bool room = out != nullptr && capacity > 0 && length < capacity;
        va_list ap;
        va_start(ap, fmt);
        // vsnprintf returns the full length even when it truncates, which is what the caller sizes a buffer from.
        const int n = std::vsnprintf(room ? out + length : nullptr, room ? capacity - length : 0, fmt, ap);
        va_end(ap);
        if (n > 0) length += static_cast<usize>(n);
    }
    void Finish() {
        if (out != nullptr && capacity > 0) out[length < capacity ? length : capacity - 1] = 0;
    }
};

// Escapes a name for a Graphviz double-quoted string.
void DotText(const char* name, char* out, usize capacity) {
    usize n = 0;
    for (; *name != 0 && n + 2 < capacity; ++name) {
        if (*name == '"' || *name == '\\') out[n++] = '\\';
        out[n++] = *name;
    }
    out[n] = 0;
}

} // namespace

rhi::ResourceState StateForUsage(GraphUsage usage) {
    switch (usage) {
    case GraphUsage::ColorAttachment: return rhi::ResourceState::ColorAttachment;
    case GraphUsage::DepthAttachment: return rhi::ResourceState::DepthAttachment;
    case GraphUsage::DepthRead: return rhi::ResourceState::DepthRead;
    case GraphUsage::Sampled: return rhi::ResourceState::ShaderRead;
    case GraphUsage::StorageRead: return rhi::ResourceState::ShaderRead;
    case GraphUsage::StorageWrite: return rhi::ResourceState::ShaderWrite;
    case GraphUsage::CopySrc: return rhi::ResourceState::CopySrc;
    case GraphUsage::CopyDst: return rhi::ResourceState::CopyDst;
    case GraphUsage::UniformBuffer: return rhi::ResourceState::UniformBuffer;
    case GraphUsage::VertexBuffer: return rhi::ResourceState::VertexBuffer;
    case GraphUsage::IndexBuffer: return rhi::ResourceState::IndexBuffer;
    default: return rhi::ResourceState::Undefined;
    }
}

bool UsageWrites(GraphUsage usage) {
    return usage == GraphUsage::ColorAttachment || usage == GraphUsage::DepthAttachment || usage == GraphUsage::StorageWrite ||
           usage == GraphUsage::CopyDst;
}

const char* UsageName(GraphUsage usage) {
    switch (usage) {
    case GraphUsage::ColorAttachment: return "color";
    case GraphUsage::DepthAttachment: return "depth";
    case GraphUsage::DepthRead: return "depth-read";
    case GraphUsage::Sampled: return "sampled";
    case GraphUsage::StorageRead: return "storage-read";
    case GraphUsage::StorageWrite: return "storage-write";
    case GraphUsage::CopySrc: return "copy-src";
    case GraphUsage::CopyDst: return "copy-dst";
    case GraphUsage::UniformBuffer: return "uniform";
    case GraphUsage::VertexBuffer: return "vertex";
    case GraphUsage::IndexBuffer: return "index";
    default: return "?";
    }
}

const char* StateName(rhi::ResourceState state) {
    switch (state) {
    case rhi::ResourceState::Undefined: return "undefined";
    case rhi::ResourceState::VertexBuffer: return "vertex";
    case rhi::ResourceState::IndexBuffer: return "index";
    case rhi::ResourceState::UniformBuffer: return "uniform";
    case rhi::ResourceState::ShaderRead: return "shader-read";
    case rhi::ResourceState::ShaderWrite: return "shader-write";
    case rhi::ResourceState::CopySrc: return "copy-src";
    case rhi::ResourceState::CopyDst: return "copy-dst";
    case rhi::ResourceState::ColorAttachment: return "color";
    case rhi::ResourceState::DepthAttachment: return "depth";
    case rhi::ResourceState::DepthRead: return "depth-read";
    case rhi::ResourceState::Present: return "present";
    default: return "?";
    }
}

// ---- records ----

struct RenderGraph::PassRec {
    const char* name = "";
    PassExecuteFn execute = nullptr;
    void* user = nullptr;
    PassFlags flags = PassFlags::None;
    u32 first_access = 0;
    u32 access_count = 0;
    bool culled = false;
    u32 position = kNone;
};

struct RenderGraph::ResourceRec {
    const char* name = "";
    GraphResourceKind kind = GraphResourceKind::Texture;
    bool import = false;
    bool read_only = false;
    rhi::TextureDesc texture_desc;
    rhi::BufferDesc buffer_desc;
    rhi::TextureHandle texture;
    rhi::BufferHandle buffer;
    rhi::ResourceState initial = rhi::ResourceState::Undefined;
    rhi::ResourceState final_state = rhi::ResourceState::Undefined;
    u32 first_use = kNone;
    u32 last_use = kNone;
    rhi::ResourceState end_state = rhi::ResourceState::Undefined;
    rhi::TextureHandle resolved_texture;
    rhi::BufferHandle resolved_buffer;
    // Execution's bookkeeping: the state the pooled resource was taken in, and whether it is out of the pool right now.
    rhi::ResourceState pooled_state = rhi::ResourceState::Undefined;
    bool held = false;
};

struct RenderGraph::BarrierRec {
    u32 resource = 0;
    u32 position = 0;
    rhi::ResourceState before = rhi::ResourceState::Undefined;
    rhi::ResourceState after = rhi::ResourceState::Undefined;
};

struct RenderGraph::AccessRec {
    u32 resource = 0;
    GraphUsage usage = GraphUsage::Sampled;
    GraphAccess access = GraphAccess::Read;
};

struct RenderGraph::EdgeRec {
    u32 from = 0;
    u32 to = 0;
    u32 resource = kNone;
    u8 kind = kRaw;
};

struct RenderGraph::DependencyRec {
    u32 before = 0;
    u32 after = 0;
};

namespace {

template <typename T>
T* AllocArray(ArenaAllocator* arena, usize count) {
    if (count == 0) count = 1;
    void* p = arena->Alloc(sizeof(T) * count, alignof(T) > 8 ? alignof(T) : 8);
    if (p == nullptr) return nullptr;
    T* items = static_cast<T*>(p);
    for (usize i = 0; i < count; ++i) new (&items[i]) T();
    return items;
}

} // namespace

// ---- construction and declaration ----

RenderGraph::RenderGraph(ArenaAllocator* arena, const RenderGraphDesc& desc) : arena_(arena), desc_(desc) {
    ENGINE_ASSERT(arena != nullptr, "RenderGraph: the arena is null");
    ENGINE_ASSERT(desc.max_passes > 0 && desc.max_resources > 0 && desc.max_accesses > 0 && desc.max_dependencies > 0, "RenderGraph: a capacity is 0");
    passes_ = AllocArray<PassRec>(arena, desc.max_passes);
    resources_ = AllocArray<ResourceRec>(arena, desc.max_resources);
    accesses_ = AllocArray<AccessRec>(arena, desc.max_accesses);
    dependencies_ = AllocArray<DependencyRec>(arena, desc.max_dependencies);
    schedule_ = AllocArray<u32>(arena, desc.max_passes);
    errors_ = static_cast<char(*)[kGraphErrorLength]>(arena->Alloc(sizeof(char[kGraphErrorLength]) * kMaxGraphErrors, 8));
    valid_ = passes_ && resources_ && accesses_ && dependencies_ && schedule_ && errors_;
}

void RenderGraph::AddError(const char* fmt, ...) {
    if (error_count_ >= kMaxGraphErrors || errors_ == nullptr) {
        ++dropped_errors_;
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(errors_[error_count_], kGraphErrorLength, fmt, ap);
    va_end(ap);
    ++error_count_;
}

const char* RenderGraph::Error(u32 index) const {
    ENGINE_ASSERT(index < error_count_, "RenderGraph::Error: index out of range");
    return errors_[index];
}

GraphResource RenderGraph::AddResource(const char* name, GraphResourceKind kind, bool import, bool read_only) {
    if (!valid_) return GraphResource{};
    compiled_ = false;
    const char* n = name != nullptr ? name : "(unnamed)";
    if (resource_count_ == desc_.max_resources) {
        AddError("resource '%s' not added: the resource table is full (max_resources %u)", n, desc_.max_resources);
        return GraphResource{};
    }
    ResourceRec& r = resources_[resource_count_];
    r = ResourceRec{};
    r.name = n;
    r.kind = kind;
    r.import = import;
    r.read_only = read_only;
    GraphResource id;
    id.index = resource_count_++;
    return id;
}

GraphResource RenderGraph::CreateTexture(const char* name, const rhi::TextureDesc& desc) {
    const GraphResource r = AddResource(name, GraphResourceKind::Texture, false, false);
    if (r.IsValid()) resources_[r.index].texture_desc = desc;
    return r;
}

GraphResource RenderGraph::CreateBuffer(const char* name, const rhi::BufferDesc& desc) {
    const GraphResource r = AddResource(name, GraphResourceKind::Buffer, false, false);
    if (r.IsValid()) resources_[r.index].buffer_desc = desc;
    return r;
}

GraphResource RenderGraph::ImportTexture(const char* name, rhi::TextureHandle texture, rhi::ResourceState initial, rhi::ResourceState final_state,
                                         bool read_only) {
    if (!texture.IsValid()) {
        AddError("import '%s' has an invalid texture handle", name != nullptr ? name : "(unnamed)");
        return GraphResource{};
    }
    const GraphResource r = AddResource(name, GraphResourceKind::Texture, true, read_only);
    if (r.IsValid()) {
        ResourceRec& rec = resources_[r.index];
        rec.texture = texture;
        rec.initial = initial;
        rec.final_state = final_state;
    }
    return r;
}

GraphResource RenderGraph::ImportBuffer(const char* name, rhi::BufferHandle buffer, rhi::ResourceState initial, rhi::ResourceState final_state,
                                        bool read_only) {
    if (!buffer.IsValid()) {
        AddError("import '%s' has an invalid buffer handle", name != nullptr ? name : "(unnamed)");
        return GraphResource{};
    }
    const GraphResource r = AddResource(name, GraphResourceKind::Buffer, true, read_only);
    if (r.IsValid()) {
        ResourceRec& rec = resources_[r.index];
        rec.buffer = buffer;
        rec.initial = initial;
        rec.final_state = final_state;
    }
    return r;
}

const rhi::TextureDesc& RenderGraph::TextureDescOf(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_ && resources_[resource.index].kind == GraphResourceKind::Texture,
                  "RenderGraph::TextureDescOf: not a texture of this graph");
    return resources_[resource.index].texture_desc;
}

const rhi::BufferDesc& RenderGraph::BufferDescOf(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_ && resources_[resource.index].kind == GraphResourceKind::Buffer,
                  "RenderGraph::BufferDescOf: not a buffer of this graph");
    return resources_[resource.index].buffer_desc;
}

GraphResourceKind RenderGraph::KindOf(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::KindOf: invalid resource");
    return resources_[resource.index].kind;
}

bool RenderGraph::IsImport(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::IsImport: invalid resource");
    return resources_[resource.index].import;
}

rhi::TextureHandle RenderGraph::ImportedTexture(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::ImportedTexture: invalid resource");
    return resources_[resource.index].texture;
}

rhi::BufferHandle RenderGraph::ImportedBuffer(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::ImportedBuffer: invalid resource");
    return resources_[resource.index].buffer;
}

rhi::ResourceState RenderGraph::InitialState(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::InitialState: invalid resource");
    return resources_[resource.index].initial;
}

rhi::ResourceState RenderGraph::FinalState(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::FinalState: invalid resource");
    return resources_[resource.index].final_state;
}

PassBuilder RenderGraph::AddPass(const char* name, PassExecuteFn execute, void* user, PassFlags flags) {
    if (!valid_) return PassBuilder(this, PassId{});
    compiled_ = false;
    const char* n = name != nullptr ? name : "(unnamed)";
    if (pass_count_ == desc_.max_passes) {
        AddError("pass '%s' not added: the pass table is full (max_passes %u)", n, desc_.max_passes);
        return PassBuilder(this, PassId{});
    }
    PassRec& p = passes_[pass_count_];
    p = PassRec{};
    p.name = n;
    p.execute = execute;
    p.user = user;
    p.flags = flags;
    p.first_access = access_count_;
    PassId id;
    id.index = pass_count_++;
    return PassBuilder(this, id);
}

void RenderGraph::AddDependency(PassId before, PassId after) {
    if (!valid_) return;
    compiled_ = false;
    if (!before.IsValid() || !after.IsValid() || before.index >= pass_count_ || after.index >= pass_count_) {
        AddError("a dependency names a pass that does not exist");
        return;
    }
    if (before.index == after.index) {
        AddError("pass '%s' cannot depend on itself", passes_[before.index].name);
        return;
    }
    if (dependency_count_ == desc_.max_dependencies) {
        AddError("dependency '%s' -> '%s' not added: the dependency table is full", passes_[before.index].name, passes_[after.index].name);
        return;
    }
    dependencies_[dependency_count_].before = before.index;
    dependencies_[dependency_count_].after = after.index;
    ++dependency_count_;
}

PassBuilder& PassBuilder::Read(GraphResource resource, GraphUsage usage) { return Add(resource, usage, GraphAccess::Read); }

PassBuilder& PassBuilder::Write(GraphResource resource, GraphUsage usage) { return Add(resource, usage, GraphAccess::Write); }

PassBuilder& PassBuilder::ReadWrite(GraphResource resource, GraphUsage usage) { return Add(resource, usage, GraphAccess::ReadWrite); }

PassBuilder& PassBuilder::Add(GraphResource resource, GraphUsage usage, GraphAccess access) {
    graph_->AddAccess(pass_, resource, usage, access);
    return *this;
}

bool RenderGraph::AddAccess(PassId pass, GraphResource resource, GraphUsage usage, GraphAccess access) {
    if (!valid_ || !pass.IsValid()) return false; // the pass table was full; that is already an error
    compiled_ = false;
    PassRec& p = passes_[pass.index];
    const char* pname = p.name;
    if (pass.index + 1 != pass_count_) {
        AddError("pass '%s': its accesses must be declared before the next pass is added", pname);
        return false;
    }
    if (!resource.IsValid() || resource.index >= resource_count_) {
        AddError("pass '%s' uses a resource that was never created (or whose creation failed)", pname);
        return false;
    }
    const ResourceRec& r = resources_[resource.index];
    if (static_cast<u32>(usage) >= static_cast<u32>(GraphUsage::Count)) {
        AddError("pass '%s' uses '%s' with an unknown usage", pname, r.name);
        return false;
    }
    const bool kind_ok = r.kind == GraphResourceKind::Texture ? TextureUsageOk(usage) : BufferUsageOk(usage);
    if (!kind_ok) {
        AddError("pass '%s': usage '%s' does not apply to the %s '%s'", pname, UsageName(usage),
                 r.kind == GraphResourceKind::Texture ? "texture" : "buffer", r.name);
        return false;
    }
    const bool writes_usage = UsageWrites(usage);
    if (writes_usage == (access == GraphAccess::Read)) {
        AddError("pass '%s': usage '%s' of '%s' is %s, so it cannot be declared with %s", pname, UsageName(usage), r.name,
                 writes_usage ? "a write" : "a read", AccessName(access));
        return false;
    }
    if (access != GraphAccess::Read && r.read_only) {
        AddError("pass '%s' writes '%s', which is a read-only import", pname, r.name);
        return false;
    }
    for (u32 i = 0; i < p.access_count; ++i) {
        if (accesses_[p.first_access + i].resource == resource.index) {
            AddError("pass '%s' declares '%s' twice; use ReadWrite for one access that reads and writes", pname, r.name);
            return false;
        }
    }
    if (access_count_ == desc_.max_accesses) {
        AddError("pass '%s': the access table is full (max_accesses %u)", pname, desc_.max_accesses);
        return false;
    }
    AccessRec& a = accesses_[access_count_++];
    a.resource = resource.index;
    a.usage = usage;
    a.access = access;
    ++p.access_count;
    return true;
}

// ---- queries ----

PassId RenderGraph::Scheduled(u32 position) const {
    ENGINE_ASSERT(compiled_ && position < schedule_count_, "RenderGraph::Scheduled: no schedule or position out of range");
    PassId id;
    id.index = schedule_[position];
    return id;
}

bool RenderGraph::IsCulled(PassId pass) const {
    ENGINE_ASSERT(compiled_ && pass.IsValid() && pass.index < pass_count_, "RenderGraph::IsCulled: not compiled or invalid pass");
    return passes_[pass.index].culled;
}

const char* RenderGraph::PassName(PassId pass) const {
    ENGINE_ASSERT(pass.IsValid() && pass.index < pass_count_, "RenderGraph::PassName: invalid pass");
    return passes_[pass.index].name;
}

PassExecuteFn RenderGraph::PassCallback(PassId pass) const {
    ENGINE_ASSERT(pass.IsValid() && pass.index < pass_count_, "RenderGraph::PassCallback: invalid pass");
    return passes_[pass.index].execute;
}

void* RenderGraph::PassUser(PassId pass) const {
    ENGINE_ASSERT(pass.IsValid() && pass.index < pass_count_, "RenderGraph::PassUser: invalid pass");
    return passes_[pass.index].user;
}

const char* RenderGraph::ResourceName(GraphResource resource) const {
    ENGINE_ASSERT(resource.IsValid() && resource.index < resource_count_, "RenderGraph::ResourceName: invalid resource");
    return resources_[resource.index].name;
}

u32 RenderGraph::FirstUse(GraphResource resource) const {
    ENGINE_ASSERT(compiled_ && resource.IsValid() && resource.index < resource_count_, "RenderGraph::FirstUse: not compiled or invalid resource");
    return resources_[resource.index].first_use;
}

u32 RenderGraph::LastUse(GraphResource resource) const {
    ENGINE_ASSERT(compiled_ && resource.IsValid() && resource.index < resource_count_, "RenderGraph::LastUse: not compiled or invalid resource");
    return resources_[resource.index].last_use;
}

// ---- compile ----

bool RenderGraph::Compile() {
    if (compiled_) return true; // nothing was declared since the last successful Compile
    schedule_count_ = 0;
    edge_count_ = 0;
    if (!valid_) {
        AddError("the render graph could not reserve its tables in the arena");
        return false;
    }
    if (error_count_ > 0) return false;

    const u32 P = pass_count_;
    const u32 R = resource_count_;
    const u32 edge_capacity = 2 * access_count_ + dependency_count_;
    edges_ = AllocArray<EdgeRec>(arena_, edge_capacity);
    u32* last_writer = AllocArray<u32>(arena_, R);
    u32* reader_head = AllocArray<u32>(arena_, R);
    u32* node_next = AllocArray<u32>(arena_, access_count_);
    u32* node_pass = AllocArray<u32>(arena_, access_count_);
    u32* out_start = AllocArray<u32>(arena_, P + 1);
    u32* in_start = AllocArray<u32>(arena_, P + 1);
    u32* out_edges = AllocArray<u32>(arena_, edge_capacity);
    u32* in_edges = AllocArray<u32>(arena_, edge_capacity);
    u32* indegree = AllocArray<u32>(arena_, P);
    u32* heap = AllocArray<u32>(arena_, P);
    u32* order = AllocArray<u32>(arena_, P);
    u8* live = AllocArray<u8>(arena_, P);
    u32* cursor = AllocArray<u32>(arena_, P + 1);
    if (!edges_ || !last_writer || !reader_head || !node_next || !node_pass || !out_start || !in_start || !out_edges || !in_edges || !indegree || !heap ||
        !order || !live || !cursor) {
        AddError("compiling the render graph ran out of arena space");
        return false;
    }
    for (u32 r = 0; r < R; ++r) {
        last_writer[r] = kNone;
        reader_head[r] = kNone;
    }

    auto add_edge = [&](u32 from, u32 to, u8 kind, u32 resource) {
        EdgeRec& e = edges_[edge_count_++];
        e.from = from;
        e.to = to;
        e.kind = kind;
        e.resource = resource;
    };

    // Edges from declaration order.
    for (u32 p = 0; p < P; ++p) {
        const PassRec& pass = passes_[p];
        for (u32 i = 0; i < pass.access_count; ++i) {
            const u32 ai = pass.first_access + i;
            const AccessRec& a = accesses_[ai];
            const u32 r = a.resource;
            const bool reads = a.access != GraphAccess::Write;
            const bool writes = a.access != GraphAccess::Read;
            if (reads) {
                if (last_writer[r] != kNone) {
                    add_edge(last_writer[r], p, kRaw, r);
                } else if (!resources_[r].import) {
                    AddError("pass '%s' reads '%s' but no earlier pass writes it", pass.name, resources_[r].name);
                }
            }
            if (writes) {
                if (last_writer[r] != kNone && !reads) add_edge(last_writer[r], p, kWaw, r);
                for (u32 n = reader_head[r]; n != kNone; n = node_next[n]) add_edge(node_pass[n], p, kWar, r);
                reader_head[r] = kNone;
                last_writer[r] = p;
            } else {
                node_pass[ai] = p;
                node_next[ai] = reader_head[r];
                reader_head[r] = ai;
            }
        }
    }
    if (error_count_ > 0) return false;
    for (u32 d = 0; d < dependency_count_; ++d) add_edge(dependencies_[d].before, dependencies_[d].after, kAfter, kNone);

    // Adjacency in both directions.
    for (u32 e = 0; e < edge_count_; ++e) {
        ++out_start[edges_[e].from + 1];
        ++in_start[edges_[e].to + 1];
        ++indegree[edges_[e].to];
    }
    for (u32 p = 0; p < P; ++p) {
        out_start[p + 1] += out_start[p];
        in_start[p + 1] += in_start[p];
    }
    for (u32 p = 0; p <= P; ++p) cursor[p] = out_start[p];
    for (u32 e = 0; e < edge_count_; ++e) out_edges[cursor[edges_[e].from]++] = e;
    for (u32 p = 0; p <= P; ++p) cursor[p] = in_start[p];
    for (u32 e = 0; e < edge_count_; ++e) in_edges[cursor[edges_[e].to]++] = e;

    // Kahn's algorithm, always taking the smallest declaration index among the ready passes.
    u32 heap_size = 0;
    auto push = [&](u32 v) {
        u32 i = heap_size++;
        while (i > 0 && heap[(i - 1) / 2] > v) {
            heap[i] = heap[(i - 1) / 2];
            i = (i - 1) / 2;
        }
        heap[i] = v;
    };
    auto pop = [&]() {
        const u32 top = heap[0];
        const u32 v = heap[--heap_size];
        u32 i = 0;
        for (;;) {
            u32 c = 2 * i + 1;
            if (c >= heap_size) break;
            if (c + 1 < heap_size && heap[c + 1] < heap[c]) ++c;
            if (heap[c] >= v) break;
            heap[i] = heap[c];
            i = c;
        }
        if (heap_size > 0) heap[i] = v;
        return top;
    };
    for (u32 p = 0; p < P; ++p) {
        if (indegree[p] == 0) push(p);
    }
    u32 ordered = 0;
    while (heap_size > 0) {
        const u32 p = pop();
        order[ordered++] = p;
        for (u32 k = out_start[p]; k < out_start[p + 1]; ++k) {
            const u32 to = edges_[out_edges[k]].to;
            if (--indegree[to] == 0) push(to);
        }
    }
    if (ordered < P) {
        // Every pass left over has a predecessor that is also left over; walk predecessors until one repeats.
        u32 start = kNone;
        for (u32 p = 0; p < P && start == kNone; ++p) {
            if (indegree[p] > 0) start = p;
        }
        u32* seen_at = AllocArray<u32>(arena_, P);
        u32* path = AllocArray<u32>(arena_, P + 1);
        if (seen_at == nullptr || path == nullptr) {
            AddError("dependency cycle (no arena space to name it)");
            return false;
        }
        for (u32 p = 0; p < P; ++p) seen_at[p] = kNone;
        u32 len = 0;
        u32 cur = start;
        while (seen_at[cur] == kNone) {
            seen_at[cur] = len;
            path[len++] = cur;
            u32 next = kNone;
            for (u32 k = in_start[cur]; k < in_start[cur + 1] && next == kNone; ++k) {
                const u32 from = edges_[in_edges[k]].from;
                if (indegree[from] > 0) next = from;
            }
            cur = next;
        }
        // path[seen_at[cur]..len) walked predecessors, so read it backwards for the forward cycle.
        char text[kGraphErrorLength];
        usize n = static_cast<usize>(std::snprintf(text, sizeof(text), "dependency cycle: "));
        const u32 first = seen_at[cur];
        u32 shown = 0;
        for (u32 i = len; i > first && n + 1 < sizeof(text); --i) {
            if (shown == 8) {
                n += static_cast<usize>(std::snprintf(text + n, sizeof(text) - n, "... -> "));
                break;
            }
            n += static_cast<usize>(std::snprintf(text + n, sizeof(text) - n, "%s -> ", passes_[path[i - 1]].name));
            ++shown;
        }
        if (n + 1 < sizeof(text)) std::snprintf(text + n, sizeof(text) - n, "%s", passes_[path[len - 1]].name);
        AddError("%s", text);
        return false;
    }

    // Liveness: side effects and writes to imports are roots; a live pass keeps the writers it reads from.
    for (u32 p = 0; p < P; ++p) {
        const PassRec& pass = passes_[p];
        bool root = (pass.flags & PassFlags::SideEffects);
        for (u32 i = 0; i < pass.access_count && !root; ++i) {
            const AccessRec& a = accesses_[pass.first_access + i];
            if (a.access != GraphAccess::Read && resources_[a.resource].import) root = true;
        }
        live[p] = root ? 1 : 0;
    }
    for (u32 i = P; i > 0; --i) {
        const u32 p = order[i - 1];
        if (!live[p]) continue;
        for (u32 k = in_start[p]; k < in_start[p + 1]; ++k) {
            const EdgeRec& e = edges_[in_edges[k]];
            if (e.kind == kRaw) live[e.from] = 1;
        }
    }
    for (u32 i = 0; i < P; ++i) {
        const u32 p = order[i];
        passes_[p].culled = live[p] == 0;
        passes_[p].position = kNone;
        if (live[p]) {
            passes_[p].position = schedule_count_;
            schedule_[schedule_count_++] = p;
        }
    }
    for (u32 r = 0; r < R; ++r) {
        resources_[r].first_use = kNone;
        resources_[r].last_use = kNone;
    }
    for (u32 pos = 0; pos < schedule_count_; ++pos) {
        const PassRec& pass = passes_[schedule_[pos]];
        for (u32 i = 0; i < pass.access_count; ++i) {
            ResourceRec& res = resources_[accesses_[pass.first_access + i].resource];
            if (res.first_use == kNone) res.first_use = pos;
            res.last_use = pos;
        }
    }
    if (!PlanBarriers()) return false;
    compiled_ = true;
    return true;
}

namespace {

bool TextureUsageFlag(GraphUsage u, rhi::TextureUsage have) {
    switch (u) {
    case GraphUsage::ColorAttachment: return rhi::Has(have, rhi::TextureUsage::ColorAttachment);
    case GraphUsage::DepthAttachment: return rhi::Has(have, rhi::TextureUsage::DepthAttachment);
    // Depth read-only is both the depth test without writes and sampling the depth, so either flag allows it.
    case GraphUsage::DepthRead: return rhi::Has(have, rhi::TextureUsage::DepthAttachment) || rhi::Has(have, rhi::TextureUsage::Sampled);
    case GraphUsage::Sampled: return rhi::Has(have, rhi::TextureUsage::Sampled);
    case GraphUsage::StorageRead:
    case GraphUsage::StorageWrite: return rhi::Has(have, rhi::TextureUsage::Storage);
    case GraphUsage::CopySrc: return rhi::Has(have, rhi::TextureUsage::CopySrc);
    case GraphUsage::CopyDst: return rhi::Has(have, rhi::TextureUsage::CopyDst);
    default: return false;
    }
}

bool BufferUsageFlag(GraphUsage u, rhi::BufferUsage have) {
    switch (u) {
    case GraphUsage::UniformBuffer: return rhi::Has(have, rhi::BufferUsage::Uniform);
    case GraphUsage::VertexBuffer: return rhi::Has(have, rhi::BufferUsage::Vertex);
    case GraphUsage::IndexBuffer: return rhi::Has(have, rhi::BufferUsage::Index);
    case GraphUsage::StorageRead:
    case GraphUsage::StorageWrite: return rhi::Has(have, rhi::BufferUsage::Storage);
    case GraphUsage::CopySrc: return rhi::Has(have, rhi::BufferUsage::CopySrc);
    case GraphUsage::CopyDst: return rhi::Has(have, rhi::BufferUsage::CopyDst);
    default: return false;
    }
}

} // namespace

bool RenderGraph::PlanBarriers() {
    const u32 R = resource_count_;
    barrier_count_ = 0;
    // Transient resources must have been declared with the usage flags every scheduled use needs.
    bool ok = true;
    for (u32 pos = 0; pos < schedule_count_; ++pos) {
        const PassRec& pass = passes_[schedule_[pos]];
        for (u32 i = 0; i < pass.access_count; ++i) {
            const AccessRec& a = accesses_[pass.first_access + i];
            const ResourceRec& res = resources_[a.resource];
            if (res.import) continue;
            const bool fits = res.kind == GraphResourceKind::Texture ? TextureUsageFlag(a.usage, res.texture_desc.usage)
                                                                     : BufferUsageFlag(a.usage, res.buffer_desc.usage);
            if (!fits) {
                AddError("pass '%s' uses transient %s '%s' as %s, but its description lacks the matching usage flag", pass.name,
                         res.kind == GraphResourceKind::Texture ? "texture" : "buffer", res.name, UsageName(a.usage));
                ok = false;
            }
        }
    }
    if (!ok) return false;

    u32 capacity = 0;
    for (u32 pos = 0; pos < schedule_count_; ++pos) capacity += passes_[schedule_[pos]].access_count;
    capacity += R;
    barriers_ = AllocArray<BarrierRec>(arena_, capacity);
    rhi::ResourceState* current = AllocArray<rhi::ResourceState>(arena_, R);
    bool* wrote = AllocArray<bool>(arena_, R);
    acq_order_ = AllocArray<u32>(arena_, R);
    barrier_start_ = AllocArray<u32>(arena_, schedule_count_ + 2);
    list_scratch_ = AllocArray<rhi::CommandList*>(arena_, schedule_count_ + 1);
    if (barriers_ == nullptr || current == nullptr || wrote == nullptr || acq_order_ == nullptr || barrier_start_ == nullptr || list_scratch_ == nullptr) {
        AddError("the render graph ran out of arena space planning barriers");
        return false;
    }
    for (u32 r = 0; r < R; ++r) {
        current[r] = resources_[r].import ? resources_[r].initial : rhi::ResourceState::Undefined;
        wrote[r] = false;
        resources_[r].end_state = rhi::ResourceState::Undefined;
    }
    for (u32 pos = 0; pos < schedule_count_; ++pos) {
        const PassRec& pass = passes_[schedule_[pos]];
        for (u32 i = 0; i < pass.access_count; ++i) {
            const AccessRec& a = accesses_[pass.first_access + i];
            const rhi::ResourceState want = StateForUsage(a.usage);
            if (current[a.resource] != want || wrote[a.resource]) {
                BarrierRec& b = barriers_[barrier_count_++];
                b.resource = a.resource;
                b.position = pos;
                b.before = current[a.resource];
                b.after = want;
            }
            current[a.resource] = want;
            wrote[a.resource] = UsageWrites(a.usage);
        }
    }
    for (u32 r = 0; r < R; ++r) {
        ResourceRec& res = resources_[r];
        res.end_state = current[r];
        if (res.import && current[r] != res.final_state) {
            BarrierRec& b = barriers_[barrier_count_++];
            b.resource = r;
            b.position = schedule_count_;
            b.before = current[r];
            b.after = res.final_state;
        }
    }
    // Barriers are ordered by position (the exit barriers last), so each position owns a contiguous run.
    u32 next = 0;
    for (u32 pos = 0; pos <= schedule_count_; ++pos) {
        barrier_start_[pos] = next;
        while (next < barrier_count_ && barriers_[next].position == pos) ++next;
    }
    barrier_start_[schedule_count_ + 1] = barrier_count_;
    return true;
}

GraphBarrier RenderGraph::BarrierAt(u32 index) const {
    ENGINE_ASSERT(index < barrier_count_, "RenderGraph::BarrierAt: index out of range");
    const BarrierRec& b = barriers_[index];
    GraphBarrier out;
    out.resource.index = b.resource;
    out.position = b.position;
    out.before = b.before;
    out.after = b.after;
    return out;
}

rhi::ResourceState RenderGraph::StateAfterLastUse(GraphResource resource) const {
    ENGINE_ASSERT(compiled_, "RenderGraph::StateAfterLastUse: Compile has not succeeded");
    ENGINE_ASSERT(resource.index < resource_count_, "RenderGraph::StateAfterLastUse: bad resource");
    return resources_[resource.index].end_state;
}

usize RenderGraph::DumpBarriers(char* out, usize capacity) const {
    RenderGraphWriter w{out, capacity};
    if (compiled_) {
        for (u32 i = 0; i < barrier_count_; ++i) {
            const BarrierRec& b = barriers_[i];
            if (b.position == schedule_count_) w.Add("barrier at exit \"%s\" %s -> %s\n", resources_[b.resource].name, StateName(b.before), StateName(b.after));
            else w.Add("barrier at %u \"%s\" %s -> %s\n", b.position, resources_[b.resource].name, StateName(b.before), StateName(b.after));
        }
    }
    w.Finish();
    return w.length;
}

rhi::TextureHandle RenderGraph::ResolvedTexture(GraphResource resource) const {
    ENGINE_ASSERT(resource.index < resource_count_ && resources_[resource.index].kind == GraphResourceKind::Texture, "RenderGraph::ResolvedTexture: not a texture of this graph");
    const ResourceRec& res = resources_[resource.index];
    return res.import ? res.texture : res.resolved_texture;
}

rhi::BufferHandle RenderGraph::ResolvedBuffer(GraphResource resource) const {
    ENGINE_ASSERT(resource.index < resource_count_ && resources_[resource.index].kind == GraphResourceKind::Buffer, "RenderGraph::ResolvedBuffer: not a buffer of this graph");
    const ResourceRec& res = resources_[resource.index];
    return res.import ? res.buffer : res.resolved_buffer;
}

void RenderGraph::SetExecuteError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(exec_error_, sizeof(exec_error_), fmt, ap);
    va_end(ap);
}

void RenderGraph::ReleaseResource(TransientPool* pool, u32 r, rhi::ResourceState state) {
    ResourceRec& res = resources_[r];
    if (res.kind == GraphResourceKind::Texture) pool->ReleaseTexture(res.resolved_texture, state);
    else pool->ReleaseBuffer(res.resolved_buffer, state);
    res.held = false;
}

void RenderGraph::ClearResolved() {
    for (u32 r = 0; r < resource_count_; ++r) {
        resources_[r].resolved_texture = rhi::TextureHandle{};
        resources_[r].resolved_buffer = rhi::BufferHandle{};
        resources_[r].held = false;
    }
}

// Nothing was submitted, so none of the barriers will run: every transient goes back to the pool in the state it was taken
// in. A handle can serve several resources in turn; undoing in reverse acquisition order leaves the earliest, original state.
void RenderGraph::RollbackTransients(TransientPool* pool) {
    for (u32 r = 0; r < resource_count_; ++r) {
        if (resources_[r].held) ReleaseResource(pool, r, rhi::ResourceState::Undefined);
    }
    for (u32 i = acq_count_; i > 0; --i) {
        const ResourceRec& res = resources_[acq_order_[i - 1]];
        if (res.kind == GraphResourceKind::Texture) pool->SetTextureState(res.resolved_texture, res.pooled_state);
        else pool->SetBufferState(res.resolved_buffer, res.pooled_state);
    }
    ClearResolved();
}

// Assigns a pooled resource to every transient, walking the schedule: taken at the first use, given back after the last, so
// a later resource in the same frame can reuse it. Done before any recording so the passes can record in any order.
bool RenderGraph::ResolveTransients(TransientPool* pool) {
    acq_count_ = 0;
    ClearResolved();
    for (u32 r = 0; r < resource_count_; ++r) resources_[r].pooled_state = rhi::ResourceState::Undefined;
    for (u32 pos = 0; pos < schedule_count_; ++pos) {
        const PassRec& pass = passes_[schedule_[pos]];
        for (u32 i = 0; i < pass.access_count; ++i) {
            const u32 r = accesses_[pass.first_access + i].resource;
            ResourceRec& res = resources_[r];
            if (res.import || res.first_use != pos) continue;
            if (pool == nullptr) {
                SetExecuteError("pass '%s' needs transient resource '%s' but Execute was given no pool", pass.name, res.name);
                RollbackTransients(pool);
                return false;
            }
            if (res.kind == GraphResourceKind::Texture) {
                PooledTexture t;
                if (!pool->AcquireTexture(res.texture_desc, &t)) {
                    SetExecuteError("transient texture '%s' could not be allocated", res.name);
                    RollbackTransients(pool);
                    return false;
                }
                res.resolved_texture = t.handle;
                res.pooled_state = t.state;
            } else {
                PooledBuffer b;
                if (!pool->AcquireBuffer(res.buffer_desc, &b)) {
                    SetExecuteError("transient buffer '%s' could not be allocated", res.name);
                    RollbackTransients(pool);
                    return false;
                }
                res.resolved_buffer = b.handle;
                res.pooled_state = b.state;
            }
            res.held = true;
            acq_order_[acq_count_++] = r;
        }
        for (u32 i = 0; i < pass.access_count; ++i) {
            const u32 r = accesses_[pass.first_access + i].resource;
            const ResourceRec& res = resources_[r];
            if (!res.import && res.last_use == pos && res.held) ReleaseResource(pool, r, res.end_state);
        }
    }
    return true;
}

void RenderGraph::RecordBarriers(u32 position, rhi::CommandList* list) const {
    for (u32 k = barrier_start_[position]; k < barrier_start_[position + 1]; ++k) {
        const BarrierRec& b = barriers_[k];
        const ResourceRec& res = resources_[b.resource];
        // A transient's first barrier starts from the state the pooled resource was left in, so the new use is also ordered
        // after the old one; a fresh resource is Undefined.
        const bool first = !res.import && res.first_use == position;
        const rhi::ResourceState before = (first && b.before == rhi::ResourceState::Undefined) ? res.pooled_state : b.before;
        if (res.kind == GraphResourceKind::Texture) list->Barrier(ResolvedTexture(GraphResource{b.resource}), before, b.after);
        else list->Barrier(ResolvedBuffer(GraphResource{b.resource}), before, b.after);
    }
}

bool RenderGraph::PassHasWork(u32 position) const {
    return passes_[schedule_[position]].execute != nullptr || barrier_start_[position + 1] > barrier_start_[position];
}

void RenderGraph::RecordPass(u32 position, rhi::CommandList* list) const {
    RecordBarriers(position, list);
    const u32 pass_index = schedule_[position];
    const PassRec& pass = passes_[pass_index];
    if (pass.execute != nullptr) {
        PassExecuteContext context;
        context.list = list;
        context.pass_index = pass_index;
        context.graph = this;
        pass.execute(context, pass.user);
    }
}

bool RenderGraph::Execute(TransientPool* pool, rhi::CommandList* list) {
    ENGINE_ASSERT(list != nullptr, "RenderGraph::Execute: the command list is null");
    exec_error_[0] = 0;
    if (!Compile()) {
        SetExecuteError("the graph did not compile");
        return false;
    }
    if (!ResolveTransients(pool)) return false;
    for (u32 pos = 0; pos < schedule_count_; ++pos) RecordPass(pos, list);
    RecordBarriers(schedule_count_, list);
    ClearResolved();
    return true;
}

struct RenderGraph::ParallelRecord {
    const RenderGraph* graph;
    rhi::Device* device;
    rhi::CommandList** lists;
    std::atomic<bool> failed;
};

void RenderGraph::RecordRange(void* user, usize begin, usize end) {
    ParallelRecord* job = static_cast<ParallelRecord*>(user);
    for (usize i = begin; i < end; ++i) {
        job->lists[i] = nullptr;
        if (!job->graph->PassHasWork(static_cast<u32>(i))) continue; // a pure ordering marker: nothing to record
        // The list is created here, on the thread that records it: command pools are per thread.
        rhi::CommandList* list = job->device->CreateCommandList();
        job->lists[i] = list;
        if (list == nullptr) {
            job->failed.store(true, std::memory_order_relaxed);
            continue;
        }
        list->Begin();
        job->graph->RecordPass(static_cast<u32>(i), list);
        list->End();
    }
}

bool RenderGraph::ExecuteParallel(TransientPool* pool, rhi::Device* device, jobs::Scheduler* scheduler, rhi::FenceValue* fence) {
    ENGINE_ASSERT(device != nullptr, "RenderGraph::ExecuteParallel: the device is null");
    exec_error_[0] = 0;
    if (!Compile()) {
        SetExecuteError("the graph did not compile");
        return false;
    }
    if (!ResolveTransients(pool)) return false;
    const u32 passes = schedule_count_;
    const bool exit_list = barrier_start_[passes + 1] > barrier_start_[passes];
    for (u32 i = 0; i <= passes; ++i) list_scratch_[i] = nullptr;

    ParallelRecord job{this, device, list_scratch_, {false}};
    if (scheduler != nullptr && passes > 1) jobs::ParallelForBlocking(*scheduler, passes, 1, &RenderGraph::RecordRange, &job);
    else RecordRange(&job, 0, passes);
    list_scratch_[passes] = nullptr;
    if (!job.failed.load(std::memory_order_relaxed) && exit_list) {
        rhi::CommandList* list = device->CreateCommandList();
        if (list == nullptr) {
            job.failed.store(true, std::memory_order_relaxed);
        } else {
            list->Begin();
            RecordBarriers(passes, list);
            list->End();
            list_scratch_[passes] = list;
        }
    }
    // Pure ordering markers recorded nothing: close the gaps so Submit sees only real lists, still in schedule order.
    u32 count = 0;
    for (u32 i = 0; i <= passes; ++i) {
        if (list_scratch_[i] != nullptr) list_scratch_[count++] = list_scratch_[i];
    }
    if (job.failed.load(std::memory_order_relaxed)) {
        SetExecuteError("a command list could not be created (the device is lost or out of memory)");
        RollbackTransients(pool);
        return false;
    }
    const rhi::FenceValue value = device->Submit(list_scratch_, count);
    if (value == 0 && count > 0) {
        SetExecuteError("the device refused the submission (it is lost)");
        RollbackTransients(pool);
        return false;
    }
    ClearResolved();
    if (fence != nullptr) *fence = value;
    return true;
}

// ---- text output ----

usize RenderGraph::Dump(char* out, usize capacity) const {
    RenderGraphWriter w{out, capacity};
    if (compiled_) {
        w.Add("graph passes=%u resources=%u scheduled=%u culled=%u\n", pass_count_, resource_count_, schedule_count_, pass_count_ - schedule_count_);
        w.Add("schedule:");
        for (u32 i = 0; i < schedule_count_; ++i) w.Add("%s %s", i == 0 ? "" : ",", passes_[schedule_[i]].name);
        w.Add("\nculled:");
        bool first = true;
        for (u32 p = 0; p < pass_count_; ++p) {
            if (!passes_[p].culled) continue;
            w.Add("%s %s", first ? "" : ",", passes_[p].name);
            first = false;
        }
        w.Add("\n");
    } else {
        w.Add("graph passes=%u resources=%u (not compiled)\n", pass_count_, resource_count_);
    }
    for (u32 p = 0; p < pass_count_; ++p) {
        const PassRec& pass = passes_[p];
        w.Add("pass %u \"%s\"%s", p, pass.name, (pass.flags & PassFlags::SideEffects) ? " side-effects" : "");
        if (compiled_) {
            if (pass.culled) w.Add(" culled");
            else w.Add(" at %u", pass.position);
        }
        w.Add("\n");
        for (u32 i = 0; i < pass.access_count; ++i) {
            const AccessRec& a = accesses_[pass.first_access + i];
            w.Add("  %s \"%s\" %s\n", AccessName(a.access), resources_[a.resource].name, UsageName(a.usage));
        }
    }
    for (u32 r = 0; r < resource_count_; ++r) {
        const ResourceRec& res = resources_[r];
        w.Add("resource %u \"%s\" %s %s", r, res.name, res.kind == GraphResourceKind::Texture ? "texture" : "buffer",
              !res.import ? "transient" : (res.read_only ? "import-readonly" : "import"));
        if (compiled_) {
            if (res.first_use == kNone) w.Add(" unused");
            else w.Add(" use=%u..%u", res.first_use, res.last_use);
        }
        w.Add("\n");
    }
    if (compiled_) {
        for (u32 e = 0; e < edge_count_; ++e) {
            const EdgeRec& edge = edges_[e];
            w.Add("edge \"%s\" -> \"%s\" %s", passes_[edge.from].name, passes_[edge.to].name, EdgeName(edge.kind));
            if (edge.resource != kNone) w.Add(" \"%s\"", resources_[edge.resource].name);
            w.Add("\n");
        }
    }
    w.Finish();
    return w.length;
}

usize RenderGraph::WriteDot(char* out, usize capacity) const {
    RenderGraphWriter w{out, capacity};
    char a[128];
    char b[128];
    w.Add("digraph render_graph {\n  rankdir=LR;\n  node [shape=box];\n");
    for (u32 p = 0; p < pass_count_; ++p) {
        DotText(passes_[p].name, a, sizeof(a));
        if (compiled_ && passes_[p].culled) w.Add("  p%u [label=\"%s\", style=dashed];\n", p, a);
        else if (compiled_) w.Add("  p%u [label=\"%u: %s\"];\n", p, passes_[p].position, a);
        else w.Add("  p%u [label=\"%s\"];\n", p, a);
    }
    if (compiled_) {
        for (u32 e = 0; e < edge_count_; ++e) {
            const EdgeRec& edge = edges_[e];
            if (edge.resource != kNone) {
                DotText(resources_[edge.resource].name, b, sizeof(b));
                w.Add("  p%u -> p%u [label=\"%s: %s\"];\n", edge.from, edge.to, EdgeName(edge.kind), b);
            } else {
                w.Add("  p%u -> p%u [label=\"%s\"];\n", edge.from, edge.to, EdgeName(edge.kind));
            }
        }
    }
    w.Add("}\n");
    w.Finish();
    return w.length;
}

} // namespace engine_placeholder::renderer

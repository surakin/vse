#pragma once

//! The render graph: passes declare what they read and write, and compiling turns that into an ordered, culled schedule.
//!
//! Resources are virtual: a transient texture or buffer exists only as a descriptor until a later stage allocates it,
//! and an import wraps something that outlives the frame (the swapchain image, a persistent target). A pass is a name,
//! an execute callback (a function pointer and a context pointer, no `std::function`) and a list of accesses, each with a
//! usage that fixes the resource state it needs. Declaring records data only; every mistake in a graph is reported by
//! [[renderer::RenderGraph::Compile]] as a named message, never while executing.
//!
//! Semantics follow declaration order: a read sees the most recent earlier write, and a write replaces it. From that the
//! graph derives read-after-write, write-after-write and write-after-read edges, orders the passes topologically with the
//! smallest declaration index first among those ready (so the order is stable), and drops passes whose results nothing
//! uses. All storage comes from the arena the graph is given, so building and compiling a frame allocates nothing from
//! the heap.

#include "mem/arena_allocator.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// Most compile errors the graph keeps; later ones are counted but not stored.
inline constexpr u32 kMaxGraphErrors = 16;
/// Longest stored error message, in bytes including the terminator.
inline constexpr u32 kGraphErrorLength = 256;
/// Returned by the use queries for a resource no scheduled pass uses.
inline constexpr u32 kNeverUsed = 0xffffffffu;

/// A pass in a [[renderer::RenderGraph]].
///
/// Thread-safety: thread-compatible plain data.
struct PassId {
    /// Declaration index; `kInvalid` means no pass.
    u32 index = 0xffffffffu;

    /// The invalid index.
    static constexpr u32 kInvalid = 0xffffffffu;
    /// Returns whether this names a pass (it may still be from another graph).
    /// Fails: never.
    constexpr bool IsValid() const { return index != kInvalid; }
};

/// A virtual resource in a [[renderer::RenderGraph]].
///
/// Thread-safety: thread-compatible plain data.
struct GraphResource {
    /// Creation index; `kInvalid` means no resource.
    u32 index = 0xffffffffu;

    /// The invalid index.
    static constexpr u32 kInvalid = 0xffffffffu;
    /// Returns whether this names a resource (it may still be from another graph).
    /// Fails: never.
    constexpr bool IsValid() const { return index != kInvalid; }
};

/// What a graph resource is.
enum class GraphResourceKind : u8 {
    Texture = 0, ///< A texture.
    Buffer,      ///< A buffer.
};

/// How a pass uses a resource; each maps to one [[rhi::ResourceState]] (see [[renderer::StateForUsage]]).
enum class GraphUsage : u8 {
    ColorAttachment = 0, ///< Rendered to as a color attachment (texture). Write, or ReadWrite when blending or loading.
    DepthAttachment,     ///< Depth(-stencil) attachment with writes (texture). Write, or ReadWrite with a depth test.
    DepthRead,           ///< Depth read-only: the depth test without writes, or sampled (texture). Read.
    Sampled,             ///< Sampled by shaders (texture). Read.
    StorageRead,         ///< Read by shaders as a storage image or buffer. Read.
    StorageWrite,        ///< Written by shaders as a storage image or buffer. Write, or ReadWrite.
    CopySrc,             ///< Source of a copy. Read.
    CopyDst,             ///< Destination of a copy. Write.
    UniformBuffer,       ///< Read as a uniform buffer (buffer). Read.
    VertexBuffer,        ///< Read as vertex data (buffer). Read.
    IndexBuffer,         ///< Read as index data (buffer). Read.
    Count,               ///< Number of usages; not a usage.
};

/// How a pass touches a resource's contents.
enum class GraphAccess : u8 {
    Read = 0, ///< The pass reads what an earlier pass wrote.
    Write,    ///< The pass replaces the contents; the previous contents are not needed.
    ReadWrite, ///< The pass reads the previous contents and writes new ones.
};

/// Options of a pass.
enum class PassFlags : u32 {
    None = 0,         ///< No options.
    SideEffects = 1u, ///< Keep the pass even if nothing reads its outputs (readbacks, debug draws, presents).
};

/// Combines pass flags.
constexpr PassFlags operator|(PassFlags a, PassFlags b) { return static_cast<PassFlags>(static_cast<u32>(a) | static_cast<u32>(b)); }
/// Tests pass flags: true if `a` and `b` share a bit.
constexpr bool operator&(PassFlags a, PassFlags b) { return (static_cast<u32>(a) & static_cast<u32>(b)) != 0; }

/// Returns the resource state a usage needs.
///
/// Param usage: any usage.
/// Returns: the state; `Undefined` for `Count`.
/// Fails: never.
rhi::ResourceState StateForUsage(GraphUsage usage);

/// Returns whether a usage can write (`StorageWrite`, `ColorAttachment`, `DepthAttachment`, `CopyDst`).
///
/// Param usage: any usage.
/// Fails: never.
bool UsageWrites(GraphUsage usage);

/// Returns a usage's name as the dump prints it (`color`, `depth`, `depth-read`, `sampled`, ...).
///
/// Param usage: any usage.
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* UsageName(GraphUsage usage);

/// What a pass callback is given. Later days add the command list and resolved resources.
///
/// Thread-safety: thread-compatible plain data.
struct PassExecuteContext {
    /// The command list the pass records into; null until execution exists.
    rhi::CommandList* list = nullptr;
    /// The pass's declaration index.
    u32 pass_index = 0;
};

/// A pass callback: it is given a [[renderer::PassExecuteContext]] and the pointer the pass was declared with.
using PassExecuteFn = void (*)(const PassExecuteContext& context, void* user);

/// Capacities of a graph. Exceeding one is a compile error, not a crash.
///
/// Thread-safety: thread-compatible plain data.
struct RenderGraphDesc {
    /// Most passes.
    u32 max_passes = 256;
    /// Most resources.
    u32 max_resources = 256;
    /// Most accesses over all passes.
    u32 max_accesses = 2048;
    /// Most explicit ordering edges added with [[renderer::RenderGraph::AddDependency]].
    u32 max_dependencies = 64;
};

class RenderGraph;

/// Declares the accesses of one pass. Valid until the next pass is added.
///
/// Thread-safety: thread-compatible.
class PassBuilder {
public:
    /// Declares that the pass reads a resource.
    ///
    /// Param resource: a resource of this graph. An import counts as already written, so reading it needs no earlier pass.
    /// Param usage: how it is read; must be a read-only usage.
    /// Returns: this builder.
    /// Fails: a bad resource, a usage that does not suit the resource's kind or the access, or a second declaration of the
    ///   same resource in this pass; reported by `Compile`.
    PassBuilder& Read(GraphResource resource, GraphUsage usage);
    /// Declares that the pass writes a resource without needing its old contents.
    ///
    /// Param resource: a resource of this graph.
    /// Param usage: how it is written; must be a writing usage.
    /// Returns: this builder.
    /// Fails: as for [[renderer::PassBuilder::Read]], and writing a read-only import; reported by `Compile`.
    PassBuilder& Write(GraphResource resource, GraphUsage usage);
    /// Declares that the pass reads a resource and writes it back.
    ///
    /// Param resource: a resource of this graph.
    /// Param usage: how it is accessed; must be a writing usage.
    /// Returns: this builder.
    /// Fails: as for [[renderer::PassBuilder::Write]], and reading a transient resource nothing wrote before.
    PassBuilder& ReadWrite(GraphResource resource, GraphUsage usage);
    /// Returns the pass being declared.
    /// Fails: never.
    PassId Id() const { return pass_; }

private:
    friend class RenderGraph;
    PassBuilder(RenderGraph* graph, PassId pass) : graph_(graph), pass_(pass) {}
    PassBuilder& Add(GraphResource resource, GraphUsage usage, GraphAccess access);

    RenderGraph* graph_;
    PassId pass_;
};

/// A frame's pass and resource declarations and their compiled schedule.
///
/// The order of use is: create the resources, `AddPass` and declare accesses, `Compile`, then read the schedule. Names are
/// borrowed and must outlive the graph (string literals are the normal case). `Compile` takes scratch space from the arena.
///
/// Thread-safety: thread-compatible.
/// Ownership: borrows the arena, which must outlive the graph and not be reset while the graph is in use; all graph
///   storage lives in it, so the graph has nothing to free.
class RenderGraph {
public:
    /// Reserves the graph's tables in the arena.
    ///
    /// Param arena: the per-frame arena.
    /// Param desc: capacities, each at least 1.
    /// Fails: the arena is too small; [[renderer::RenderGraph::Valid]] then returns false and every declaration is
    ///   ignored.
    /// Aborts: `arena` is null or a capacity is 0.
    /// Ownership: `arena` is borrowed.
    RenderGraph(ArenaAllocator* arena, const RenderGraphDesc& desc);
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    /// Returns whether the tables were reserved.
    /// Fails: never.
    bool Valid() const { return valid_; }

    /// Declares a transient texture: it exists only for this frame and is allocated by a later stage.
    ///
    /// Param name: a name for messages; borrowed.
    /// Param desc: the texture's description.
    /// Returns: the resource, or an invalid one if a limit was hit (reported by `Compile`).
    /// Fails: the resource table is full.
    GraphResource CreateTexture(const char* name, const rhi::TextureDesc& desc);
    /// Declares a transient buffer; see [[renderer::RenderGraph::CreateTexture]].
    ///
    /// Param name: a name for messages; borrowed.
    /// Param desc: the buffer's description.
    /// Returns: the resource, or an invalid one if a limit was hit.
    /// Fails: the resource table is full.
    GraphResource CreateBuffer(const char* name, const rhi::BufferDesc& desc);
    /// Wraps an existing texture (the swapchain image, a persistent target).
    ///
    /// Param name: a name for messages; borrowed.
    /// Param texture: the texture; must be valid.
    /// Param initial: the state it is in when the graph starts.
    /// Param final_state: the state it must be left in (`Present` for a swapchain image).
    /// Param read_only: true if no pass may write it.
    /// Returns: the resource, or an invalid one if a limit was hit.
    /// Fails: the resource table is full.
    GraphResource ImportTexture(const char* name, rhi::TextureHandle texture, rhi::ResourceState initial, rhi::ResourceState final_state, bool read_only);
    /// Wraps an existing buffer; see [[renderer::RenderGraph::ImportTexture]].
    ///
    /// Param name: a name for messages; borrowed.
    /// Param buffer: the buffer; must be valid.
    /// Param initial: the state it is in when the graph starts.
    /// Param final_state: the state it must be left in.
    /// Param read_only: true if no pass may write it.
    /// Returns: the resource, or an invalid one if a limit was hit.
    /// Fails: the resource table is full.
    GraphResource ImportBuffer(const char* name, rhi::BufferHandle buffer, rhi::ResourceState initial, rhi::ResourceState final_state, bool read_only);

    /// Adds a pass and returns the builder for its accesses.
    ///
    /// Param name: a name for messages and dumps; borrowed.
    /// Param execute: the callback; may be null for a pass that is only declared (tests).
    /// Param user: passed to the callback.
    /// Param flags: pass options.
    /// Returns: a builder; its calls are ignored if the pass table is full (reported by `Compile`).
    /// Fails: the pass table is full.
    /// Ownership: `name` and `user` are borrowed and must outlive the graph.
    PassBuilder AddPass(const char* name, PassExecuteFn execute, void* user, PassFlags flags = PassFlags::None);

    /// Requires `before` to run before `after` when both run, without a shared resource. It does not keep `before` alive:
    /// a pass that must run only to be ordered should carry [[renderer::PassFlags::SideEffects]].
    ///
    /// Param before: a pass of this graph.
    /// Param after: a pass of this graph.
    /// Fails: an invalid pass, the same pass twice or a full table; reported by `Compile`. A cycle is reported too.
    void AddDependency(PassId before, PassId after);

    /// Checks the declarations, orders the passes, drops the unused ones and finds each resource's first and last use.
    ///
    /// Returns: true if the graph is valid; false after storing the errors (see `ErrorCount`). Call once per frame: a
    ///   second call with nothing declared in between returns true at once, and any declaration after a call makes the
    ///   next one run again (and take more scratch space from the arena).
    /// Fails: any error described at the declaring calls, a read of a transient resource no earlier pass writes, a
    ///   cycle through explicit dependencies, or the arena running out.
    bool Compile();

    /// Returns whether a resource is a texture or a buffer.
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    GraphResourceKind KindOf(GraphResource resource) const;
    /// Returns whether a resource is an import.
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    bool IsImport(GraphResource resource) const;
    /// Returns the description a transient texture was declared with.
    ///
    /// Param resource: a texture of this graph.
    /// Aborts: `resource` is invalid or not a texture.
    /// Ownership: borrowed from the graph.
    const rhi::TextureDesc& TextureDescOf(GraphResource resource) const;
    /// Returns the description a transient buffer was declared with.
    ///
    /// Param resource: a buffer of this graph.
    /// Aborts: `resource` is invalid or not a buffer.
    /// Ownership: borrowed from the graph.
    const rhi::BufferDesc& BufferDescOf(GraphResource resource) const;
    /// Returns the texture an import wraps (invalid for a transient resource).
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    rhi::TextureHandle ImportedTexture(GraphResource resource) const;
    /// Returns the buffer an import wraps (invalid for a transient resource).
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    rhi::BufferHandle ImportedBuffer(GraphResource resource) const;
    /// Returns the state an import is in when the graph starts (`Undefined` for a transient resource).
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    rhi::ResourceState InitialState(GraphResource resource) const;
    /// Returns the state an import must be left in (`Undefined` for a transient resource).
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    rhi::ResourceState FinalState(GraphResource resource) const;

    /// Returns the number of stored compile errors (at most [[renderer::kMaxGraphErrors]]).
    /// Fails: never.
    u32 ErrorCount() const { return error_count_; }
    /// Returns the number of errors that did not fit.
    /// Fails: never.
    u32 DroppedErrors() const { return dropped_errors_; }
    /// Returns a stored error message.
    ///
    /// Param index: below `ErrorCount()`.
    /// Returns: the message, naming the pass and resource.
    /// Aborts: `index` is out of range.
    /// Ownership: borrowed from the graph.
    const char* Error(u32 index) const;

    /// Returns the number of passes declared.
    /// Fails: never.
    u32 PassCount() const { return pass_count_; }
    /// Returns the number of resources declared.
    /// Fails: never.
    u32 ResourceCount() const { return resource_count_; }
    /// Returns the number of passes that remain after culling, in schedule order. Valid after a successful `Compile`.
    /// Fails: never.
    u32 ScheduleCount() const { return schedule_count_; }
    /// Returns the pass at a schedule position.
    ///
    /// Param position: below `ScheduleCount()`.
    /// Aborts: `position` is out of range.
    PassId Scheduled(u32 position) const;
    /// Returns whether a pass was dropped because nothing needs its results.
    ///
    /// Param pass: a pass of this graph.
    /// Aborts: `pass` is invalid, or `Compile` has not succeeded.
    bool IsCulled(PassId pass) const;
    /// Returns a pass's name.
    ///
    /// Param pass: a pass of this graph.
    /// Aborts: `pass` is invalid.
    /// Ownership: borrowed.
    const char* PassName(PassId pass) const;
    /// Returns a pass's callback.
    ///
    /// Param pass: a pass of this graph.
    /// Aborts: `pass` is invalid.
    PassExecuteFn PassCallback(PassId pass) const;
    /// Returns the pointer a pass was declared with.
    ///
    /// Param pass: a pass of this graph.
    /// Aborts: `pass` is invalid.
    /// Ownership: the pointer is the caller's; the graph never frees it.
    void* PassUser(PassId pass) const;
    /// Returns a resource's name.
    ///
    /// Param resource: a resource of this graph.
    /// Aborts: `resource` is invalid.
    /// Ownership: borrowed.
    const char* ResourceName(GraphResource resource) const;
    /// Returns the schedule position of the first scheduled pass that uses a resource.
    ///
    /// Param resource: a resource of this graph.
    /// Returns: the position, or [[renderer::kNeverUsed]] if every user was culled.
    /// Aborts: `resource` is invalid, or `Compile` has not succeeded.
    u32 FirstUse(GraphResource resource) const;
    /// Returns the schedule position of the last scheduled pass that uses a resource; see `FirstUse`.
    ///
    /// Param resource: a resource of this graph.
    /// Returns: the position, or [[renderer::kNeverUsed]].
    /// Aborts: `resource` is invalid, or `Compile` has not succeeded.
    u32 LastUse(GraphResource resource) const;

    /// Writes a text description of the graph: passes, accesses, resources and edges.
    ///
    /// The format is line oriented. After a header line (`graph passes=P resources=R scheduled=S culled=C`) come
    /// `schedule: a, b, c` and `culled: x, y` lines, then one block per pass (`pass I "name" [side-effects] at K` or
    /// `culled`, followed by one indented `read|write|readwrite "resource" usage` line per access), one line per resource
    /// (`resource I "name" texture|buffer transient|import|import-readonly use=F..L` or `unused`) and one line per edge
    /// (`edge "from" -> "to" raw|waw|war|after ["resource"]`). Before a successful `Compile` only the passes and
    /// resources are listed.
    ///
    /// Param out: the destination, or null to only measure.
    /// Param capacity: size of `out` in bytes.
    /// Returns: the length the full text needs, excluding the terminator; the output is truncated (and terminated) if it
    ///   exceeds `capacity - 1`.
    /// Fails: never.
    /// Ownership: `out` is borrowed for the call.
    usize Dump(char* out, usize capacity) const;

    /// Writes the compiled graph as Graphviz: live passes as boxes in schedule order, culled ones dashed, edges labelled.
    ///
    /// Param out: the destination, or null to only measure.
    /// Param capacity: size of `out` in bytes.
    /// Returns: the length the full text needs, excluding the terminator; truncated as for `Dump`.
    /// Fails: never.
    /// Ownership: `out` is borrowed for the call.
    usize WriteDot(char* out, usize capacity) const;

private:
    friend class PassBuilder;
    struct PassRec;
    struct ResourceRec;
    struct AccessRec;
    struct EdgeRec;
    struct DependencyRec;

    void AddError(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    bool AddAccess(PassId pass, GraphResource resource, GraphUsage usage, GraphAccess access);
    GraphResource AddResource(const char* name, GraphResourceKind kind, bool import, bool read_only);

    ArenaAllocator* arena_ = nullptr;
    RenderGraphDesc desc_;
    bool valid_ = false;
    bool compiled_ = false;
    PassRec* passes_ = nullptr;
    ResourceRec* resources_ = nullptr;
    AccessRec* accesses_ = nullptr;
    DependencyRec* dependencies_ = nullptr;
    EdgeRec* edges_ = nullptr;
    u32* schedule_ = nullptr;
    u32 pass_count_ = 0;
    u32 resource_count_ = 0;
    u32 access_count_ = 0;
    u32 dependency_count_ = 0;
    u32 edge_count_ = 0;
    u32 schedule_count_ = 0;
    u32 error_count_ = 0;
    u32 dropped_errors_ = 0;
    char (*errors_)[kGraphErrorLength] = nullptr;
};

} // namespace engine_placeholder::renderer

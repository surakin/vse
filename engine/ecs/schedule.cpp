#include "ecs/schedule.h"

#include "cstr/format.h"
#include "os/os.h"
#include "string/utf8.h"

namespace engine_placeholder::ecs {

namespace {

bool SameName(const char* a, const char* b) {
    return StringView(a) == StringView(b);
}

// Appends text to a report message, truncating at its capacity.
void Say(BuildReport* report, const char* format, const char* a, const char* b = "") {
    FormatTo(report->message, sizeof(report->message), format, a, b);
}

} // namespace

void RegisterEcsSettings(settings::Registry* registry) {
    settings::SettingDesc parallel = settings::MakeBool("ecs.parallel_systems", true);
    parallel.env = "ENGINE_ECS_PARALLEL_SYSTEMS";
    parallel.flag = "ecs-parallel-systems";
    parallel.label = "Parallel systems";
    parallel.description = "run systems whose access does not conflict on several workers (the result equals the sequential run)";
    parallel.group = "ecs";
    registry->Register(parallel);
    settings::SettingDesc stats = settings::MakeBool("ecs.stats", false);
    stats.env = "ENGINE_ECS_STATS";
    stats.flag = "ecs-stats";
    stats.label = "System statistics";
    stats.description = "time every system and count the entities and chunks its query visits";
    stats.group = "ecs";
    stats.flags = settings::kAdvanced;
    registry->Register(stats);
}

Schedule::Schedule() {
    const char* names[] = {"pre_update", "update", "post_update", "render"};
    for (u32 i = 0; i < 4; ++i) {
        stage_names_.Push(names[i]);
        stage_order_.Push(i);
    }
}

StageId Schedule::AddStage(const char* name, StageId after) {
    ENGINE_ASSERT(after < stage_names_.Size(), "Schedule::AddStage: no such stage");
    const StageId id = static_cast<StageId>(stage_names_.Size());
    stage_names_.Push(name);
    usize at = 0;
    while (at < stage_order_.Size() && stage_order_[at] != after) {
        ++at;
    }
    stage_order_.Insert(at + 1, id);
    built_ = false;
    return id;
}

void Schedule::Add(SystemDesc system) {
    ENGINE_ASSERT(system.fn_.Valid(), "Schedule::Add: the system has no function");
    ENGINE_ASSERT(system.stage_ < stage_names_.Size(), "Schedule::Add: no such stage");
    auto entry = MakeUnique<Entry>();
    entry->stats.name = system.name_;
    entry->desc = Move(system);
    systems_.Push(Move(entry));
    built_ = false;
}

BuildReport Schedule::Build() {
    BuildReport report;
    const u32 count = static_cast<u32>(systems_.Size());
    auto find = [&](const char* name) -> u32 {
        for (u32 i = 0; i < count; ++i) {
            if (SameName(systems_[i]->desc.name_, name)) {
                return i;
            }
        }
        return count;
    };
    for (u32 i = 0; i < count; ++i) {
        for (u32 j = i + 1; j < count; ++j) {
            if (SameName(systems_[i]->desc.name_, systems_[j]->desc.name_)) {
                Say(&report, "two systems are named %s%s", systems_[i]->desc.name_);
                return report;
            }
        }
    }
    // edges[i] = the systems that must run before i, within its stage.
    Vector<Vector<u32>> before;
    before.Resize(count);
    auto stage_rank = [&](StageId stage) {
        for (u32 r = 0; r < stage_order_.Size(); ++r) {
            if (stage_order_[r] == stage) {
                return r;
            }
        }
        return count;
    };
    auto add_edge = [&](u32 first, u32 second) -> bool {
        const u32 a = stage_rank(systems_[first]->desc.stage_);
        const u32 b = stage_rank(systems_[second]->desc.stage_);
        if (a > b) {
            Say(&report, "the constraint on %s puts it against the stage order%s", systems_[second]->desc.name_);
            return false;
        }
        if (a == b) {
            before[second].Push(first);
        }
        return true;
    };
    for (u32 i = 0; i < count; ++i) {
        const SystemDesc& d = systems_[i]->desc;
        for (u32 k = 0; k < d.after_count_; ++k) {
            const u32 other = find(d.after_[k]);
            if (other == count) {
                Say(&report, "system %s is told to run after %s, which does not exist", d.name_, d.after_[k]);
                return report;
            }
            if (!add_edge(other, i)) {
                return report;
            }
        }
        for (u32 k = 0; k < d.before_count_; ++k) {
            const u32 other = find(d.before_[k]);
            if (other == count) {
                Say(&report, "system %s is told to run before %s, which does not exist", d.name_, d.before_[k]);
                return report;
            }
            if (!add_edge(i, other)) {
                return report;
            }
        }
    }
    Vector<u32> order;
    Vector<u8> done;
    done.Resize(count);
    for (u32 r = 0; r < stage_order_.Size(); ++r) {
        const StageId stage = stage_order_[r];
        for (;;) {
            u32 pick = count;
            u32 remaining = 0;
            for (u32 i = 0; i < count; ++i) {
                if (done[i] != 0 || systems_[i]->desc.stage_ != stage) {
                    continue;
                }
                ++remaining;
                bool ready = true;
                for (usize k = 0; k < before[i].Size(); ++k) {
                    ready = ready && done[before[i][k]] != 0;
                }
                if (ready && pick == count) {
                    pick = i;
                }
            }
            if (remaining == 0) {
                break;
            }
            if (pick == count) {
                // Every remaining system waits for another one: follow the waits until a system repeats.
                u32 walker = count;
                for (u32 i = 0; i < count && walker == count; ++i) {
                    if (done[i] == 0 && systems_[i]->desc.stage_ == stage) {
                        walker = i;
                    }
                }
                Vector<u32> path;
                for (;;) {
                    usize seen = 0;
                    while (seen < path.Size() && path[seen] != walker) {
                        ++seen;
                    }
                    if (seen < path.Size()) {
                        char text[200] = {};
                        usize used = 0;
                        for (usize k = seen; k < path.Size() && used + 24 < sizeof(text); ++k) {
                            used += static_cast<usize>(FormatTo(text + used, sizeof(text) - used, "%s -> ", systems_[path[k]]->desc.name_));
                        }
                        FormatTo(report.message, sizeof(report.message), "the ordering constraints form a cycle: %s%s", text, systems_[walker]->desc.name_);
                        return report;
                    }
                    path.Push(walker);
                    u32 next = count;
                    for (usize k = 0; k < before[walker].Size(); ++k) {
                        if (done[before[walker][k]] == 0) {
                            next = before[walker][k];
                            break;
                        }
                    }
                    walker = next;
                }
            }
            done[pick] = 1;
            order.Push(pick);
        }
    }
    order_ = Move(order);
    built_ = true;
    report.ok = true;
    return report;
}

void Schedule::UseSettings(const settings::Registry& registry) {
    stats_setting_ = settings::Setting<bool>::Find(registry, StringView("ecs.stats"));
    parallel_setting_ = settings::Setting<bool>::Find(registry, StringView("ecs.parallel_systems"));
    has_settings_ = true;
}

void Schedule::Run(World& world, f32 dt) {
    ENGINE_ASSERT(built_, "Schedule::Run: Build() has not succeeded since the last Add");
    if (has_settings_) {
        stats_enabled_ = stats_setting_.Get();
        parallel_requested_ = parallel_setting_.Get();
    }
    const bool timed = stats_enabled_;
    usize next = 0;
    for (u32 r = 0; r < stage_order_.Size(); ++r) {
        const StageId stage = stage_order_[r];
        const usize stage_begin = next;
        while (next < order_.Size() && systems_[order_[next]]->desc.stage_ == stage) {
            Entry& e = *systems_[order_[next]];
            ++next;
            SystemContext context(&world, &e.commands, &e.desc.reads_, &e.desc.writes_, &e.desc.resource_reads_, &e.desc.resource_writes_,
                                  &stats_.access_violations, dt, stats_.frames);
            if (e.desc.condition_ != nullptr && !e.desc.condition_(context)) {
                ++e.stats.skipped_runs;
                continue;
            }
            if (timed) {
                QueryStats stale; // counts gathered while statistics were off do not belong to this run
                e.desc.fn_.TakeStats(&stale);
                const u64 begin = os::MonotonicTimeNanos();
                e.desc.fn_.Invoke(context);
                const u64 elapsed = os::MonotonicTimeNanos() - begin;
                ++e.stats.runs;
                e.stats.last_ns = elapsed;
                e.stats.total_ns += elapsed;
                QueryStats q;
                if (e.desc.fn_.TakeStats(&q)) {
                    e.stats.entities = q.entities;
                    e.stats.chunks_visited = q.chunks_visited;
                    e.stats.chunks_skipped = q.chunks_skipped;
                }
            } else {
                e.desc.fn_.Invoke(context);
            }
        }
        // The stage's command buffers apply in registration order, not run order, so the outcome does not depend on constraints that only exist for data.
        Vector<u32> by_registration;
        for (usize k = stage_begin; k < next; ++k) {
            by_registration.Push(order_[k]);
        }
        for (usize a = 1; a < by_registration.Size(); ++a) {
            for (usize b = a; b > 0 && by_registration[b - 1] > by_registration[b]; --b) {
                const u32 tmp = by_registration[b];
                by_registration[b] = by_registration[b - 1];
                by_registration[b - 1] = tmp;
            }
        }
        for (usize k = 0; k < by_registration.Size(); ++k) {
            CommandBuffer& commands = systems_[by_registration[k]]->commands;
            const PlaybackReport applied = world.Apply(commands);
            commands.Clear();
            stats_.commands_applied += applied.applied;
            stats_.commands_ignored += applied.ignored;
        }
    }
    ++stats_.frames;
    world.Advance();
}

const char* Schedule::RunOrderName(u32 index) const {
    ENGINE_ASSERT(built_ && index < order_.Size(), "Schedule::RunOrderName: not built, or no such position");
    return systems_[order_[index]]->desc.name_;
}

const SystemStats* Schedule::StatsOf(const char* name) const {
    for (usize i = 0; i < systems_.Size(); ++i) {
        if (SameName(systems_[i]->desc.name_, name)) {
            return &systems_[i]->stats;
        }
    }
    return nullptr;
}

} // namespace engine_placeholder::ecs

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

bool Overlap(const Vector<u32>& a, const Vector<u32>& b) {
    for (usize i = 0; i < a.Size(); ++i) {
        for (usize j = 0; j < b.Size(); ++j) {
            if (a[i] == b[j]) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool Schedule::Conflict(const SystemDesc& a, const SystemDesc& b) {
    if (a.writes_.Intersects(b.writes_) || a.writes_.Intersects(b.reads_) || b.writes_.Intersects(a.reads_)) {
        return true;
    }
    return Overlap(a.resource_writes_, b.resource_writes_) || Overlap(a.resource_writes_, b.resource_reads_) ||
           Overlap(b.resource_writes_, a.resource_reads_);
}

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
    // Waves: a system goes one wave after the latest earlier system of its stage that conflicts with it or must precede it, so it never overtakes either.
    Vector<u32> position;
    position.Resize(count);
    for (u32 p = 0; p < count; ++p) {
        position[order[p]] = p;
    }
    Vector<u32> wave;
    wave.Resize(count);
    for (u32 p = 0; p < count; ++p) {
        const u32 i = order[p];
        u32 w = 0;
        for (u32 q = 0; q < p; ++q) {
            const u32 j = order[q];
            if (systems_[j]->desc.stage_ != systems_[i]->desc.stage_) {
                continue;
            }
            bool must = Conflict(systems_[i]->desc, systems_[j]->desc);
            for (usize k = 0; k < before[i].Size() && !must; ++k) {
                must = before[i][k] == j;
            }
            if (must && wave[q] + 1 > w) {
                w = wave[q] + 1;
            }
        }
        wave[p] = w;
    }
    // The order the stage's command buffers apply in: the stage's systems by registration index (computed here, not every frame).
    Vector<u32> apply_order;
    for (usize k = 0; k < order.Size(); ++k) {
        apply_order.Push(order[k]);
    }
    for (u32 a = 1; a < count; ++a) {
        for (u32 b = a; b > 0 && systems_[apply_order[b - 1]]->desc.stage_ == systems_[apply_order[b]]->desc.stage_ && apply_order[b - 1] > apply_order[b]; --b) {
            const u32 tmp = apply_order[b];
            apply_order[b] = apply_order[b - 1];
            apply_order[b - 1] = tmp;
        }
    }
    order_ = Move(order);
    apply_order_ = Move(apply_order);
    wave_of_ = Move(wave);
    built_ = true;
    report.ok = true;
    return report;
}

void Schedule::UseSettings(const settings::Registry& registry) {
    stats_setting_ = settings::Setting<bool>::Find(registry, StringView("ecs.stats"));
    parallel_setting_ = settings::Setting<bool>::Find(registry, StringView("ecs.parallel_systems"));
    has_settings_ = true;
}

void Schedule::RunSystem(World& world, Entry& entry, f32 dt, jobs::Scheduler* fan_out, bool timed) {
    SystemContext context(&world, &entry.commands, &entry.desc.reads_, &entry.desc.writes_, &entry.desc.resource_reads_, &entry.desc.resource_writes_,
                          &entry.violations, &entry.parallel_commands, fan_out, dt, stats_.frames);
    if (entry.desc.condition_ != nullptr && !entry.desc.condition_(context)) {
        ++entry.stats.skipped_runs;
        return;
    }
    if (!timed) {
        entry.desc.fn_.Invoke(context);
        return;
    }
    QueryStats stale; // counts gathered while statistics were off do not belong to this run
    entry.desc.fn_.TakeStats(&stale);
    const u64 begin = os::MonotonicTimeNanos();
    entry.desc.fn_.Invoke(context);
    const u64 elapsed = os::MonotonicTimeNanos() - begin;
    ++entry.stats.runs;
    entry.stats.last_ns = elapsed;
    entry.stats.total_ns += elapsed;
    QueryStats q;
    if (entry.desc.fn_.TakeStats(&q)) {
        entry.stats.entities = q.entities;
        entry.stats.chunks_visited = q.chunks_visited;
        entry.stats.chunks_skipped = q.chunks_skipped;
    }
}

jobs::Job Schedule::SystemJob(Schedule* self, World* world, Entry* entry, f32 dt, bool timed) {
    self->RunSystem(*world, *entry, dt, nullptr, timed); // a job cannot wait for more jobs: its chunks run inline
    co_return;
}

void Schedule::ApplyStage(World& world, usize begin, usize end) {
    // Command buffers apply in registration order, not run order, so the outcome does not depend on constraints that only exist for data.
    for (usize k = begin; k < end; ++k) {
        Entry& entry = *systems_[apply_order_[k]];
        stats_.access_violations += entry.violations;
        entry.violations = 0;
        PlaybackReport applied = world.Apply(entry.commands);
        entry.commands.Clear();
        applied += entry.parallel_commands.ApplyAll(world);
        entry.parallel_commands.ClearAll();
        stats_.commands_applied += applied.applied;
        stats_.commands_ignored += applied.ignored;
    }
}

void Schedule::Run(World& world, f32 dt) {
    ENGINE_ASSERT(built_, "Schedule::Run: Build() has not succeeded since the last Add");
    if (has_settings_) {
        stats_enabled_ = stats_setting_.Get();
        parallel_requested_ = parallel_setting_.Get();
    }
    const bool timed = stats_enabled_;
    const bool parallel = parallel_requested_ && scheduler_ != nullptr && scheduler_->ParticipantCount() > 1;
    jobs::Scheduler* const fan_out = parallel ? scheduler_ : nullptr;
    usize next = 0;
    for (u32 r = 0; r < stage_order_.Size(); ++r) {
        const StageId stage = stage_order_[r];
        const usize stage_begin = next;
        while (next < order_.Size() && systems_[order_[next]]->desc.stage_ == stage) {
            ++next;
        }
        if (!parallel) {
            for (usize k = stage_begin; k < next; ++k) {
                RunSystem(world, *systems_[order_[k]], dt, nullptr, timed);
            }
        } else {
            u32 waves = 0;
            for (usize k = stage_begin; k < next; ++k) {
                waves = wave_of_[k] + 1 > waves ? wave_of_[k] + 1 : waves;
            }
            for (u32 w = 0; w < waves; ++w) {
                u32 members = 0;
                usize only = 0;
                for (usize k = stage_begin; k < next; ++k) {
                    if (wave_of_[k] == w) {
                        ++members;
                        only = k;
                    }
                }
                if (members == 1) {
                    RunSystem(world, *systems_[order_[only]], dt, fan_out, timed); // alone: may split its chunks over the workers
                    continue;
                }
                world.BeginParallel();
                jobs::Counter counter(*scheduler_);
                for (usize k = stage_begin; k < next; ++k) {
                    if (wave_of_[k] == w) {
                        jobs::Spawn(counter, SystemJob(this, &world, systems_[order_[k]].Get(), dt, timed));
                    }
                }
                scheduler_->HelpUntil(counter);
                world.EndParallel();
            }
        }
        ApplyStage(world, stage_begin, next);
    }
    ++stats_.frames;
    world.Advance();
    world.EndFrame();
}

u32 Schedule::WaveOf(u32 index) const {
    ENGINE_ASSERT(built_ && index < wave_of_.Size(), "Schedule::WaveOf: not built, or no such position");
    return wave_of_[index];
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

// The part of the Windows debugging backend that Day 156 builds (plans/phase-13-windows-dx12/), here so foundation_debug links and the
// post-mortem tests skip with a reason until then (KNOWN-ISSUES.md): post-mortem dumps through Windows Error Reporting LocalDumps.
#include "debug/post_mortem.h"

namespace engine_placeholder::debug {

PostMortemReport EnablePostMortemDumps(const PostMortemOptions&) { // Day 156
    PostMortemReport report;
    report.result = PostMortemResult::Unsupported;
    const char note[] = "Windows Error Reporting LocalDumps are configured from Phase 13 Day 156";
    for (usize i = 0; i < sizeof(note) && i < sizeof(report.note); ++i) report.note[i] = note[i];
    return report;
}

} // namespace engine_placeholder::debug

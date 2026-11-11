#pragma once

// A test-only diagnostic for death-test children on Windows (Phase 13 Day 156): if the child is still running after
// `seconds`, a watchdog thread prints every other thread's stack to standard error (suspended, walked with
// RtlVirtualUnwind, named through DbgHelp) and ends the child with exit code 0xDEAD0002, so a hang shows where it is
// instead of only timing out. Never used by the engine.
namespace hang_watchdog
{

void Start(unsigned seconds);

// In the parent: names the frames a child's watchdog wrote (lines "HANG-FRAME 0x...") and prints them to standard
// error.
void NameFrames(const char* text, unsigned long long length);

}  // namespace hang_watchdog

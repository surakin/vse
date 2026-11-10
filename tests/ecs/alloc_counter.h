#pragma once

// The heap-allocation counter of the ECS test executable (alloc_counter.cpp).

/// True when this flavor counts allocations (false where a sanitizer runtime owns operator new).
bool EcsTestCountsAllocations();
/// How many heap allocations the executable has made so far.
unsigned long EcsTestHeapAllocations();

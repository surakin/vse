#pragma once

// Test helper: checks that a test left behind no child process and no open descriptor. Construct it at the top of the
// test, call Clean() at the end. The implementation is per platform (resource_guard_linux.cpp).
class ChildResourceGuard
{
public:
    // Records how many descriptors the process has open now.
    ChildResourceGuard();

    // True if no child of this process is left running or unreaped and the descriptor count is what it was at
    // construction.
    bool Clean() const;

private:
    unsigned long descriptors_before_;
};

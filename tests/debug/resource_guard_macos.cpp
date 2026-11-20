// macOS implementation of ChildResourceGuard (Phase 21 Day 290): counts the process's descriptors with
// os::OpenHandleCount (proc_pidinfo; macOS has no /proc) and asks waitpid whether any child is left.
#include "resource_guard.h"

#include <sys/wait.h>

#include "os/last_error.h"
#include "os/os.h"

ChildResourceGuard::ChildResourceGuard() : descriptors_before_(static_cast<unsigned long>(vse::os::OpenHandleCount()))
{
}

bool ChildResourceGuard::Clean() const
{
    // -1/ECHILD means no child at all, running or zombie.
    const bool no_children = ::waitpid(-1, nullptr, WNOHANG) == -1 && vse::os::LastError() == ECHILD;
    return no_children && static_cast<unsigned long>(vse::os::OpenHandleCount()) == descriptors_before_;
}

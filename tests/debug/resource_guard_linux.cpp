// Linux implementation of ChildResourceGuard: counts /proc/self/fd and asks waitpid whether any child is left.
#include "resource_guard.h"

#include <dirent.h>
#include <sys/wait.h>

#include "os/last_error.h"

namespace
{

unsigned long CountOpenDescriptors()
{
    DIR* dir = opendir("/proc/self/fd");
    if (dir == nullptr)
    {
        return 0;
    }
    unsigned long count = 0;
    while (readdir(dir) != nullptr)
    {
        ++count;
    }
    closedir(dir);
    return count;
}

}  // namespace

ChildResourceGuard::ChildResourceGuard() : descriptors_before_(CountOpenDescriptors()) {}

bool ChildResourceGuard::Clean() const
{
    // -1/ECHILD means no child at all, running or zombie.
    const bool no_children = waitpid(-1, nullptr, WNOHANG) == -1 && vse::os::LastError() == ECHILD;
    return no_children && CountOpenDescriptors() == descriptors_before_;
}

// Linux's answers to the shared POSIX death-test backend's hooks (os/posix/debug/posix_debug.h; Phase 26 Day 307): the
// code that was in os/linux/debug/death_test.cpp, unchanged in effect.
#include "os/posix/debug/posix_debug.h"

#include "cstr/cstr.h"
#include "os/signal.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace engine_placeholder::debug::posix
{

bool MakeCloexecPipe(int fds[2])
{
    return ::pipe2(fds, O_CLOEXEC) == 0;
}

const char* SelfExecutablePath(char* buffer, usize capacity)
{
    // Executed through /proc/self/exe itself, so a test binary that was rebuilt or deleted while running still starts.
    static const char kExecutable[] = "/proc/self/exe";
    if (capacity < sizeof(kExecutable))
    {
        return nullptr;
    }
    CStrCopy(buffer, kExecutable);
    return buffer;
}

char** ProcessEnvironment()
{
    return environ;
}

bool ChildNeedsParentWatch()
{
    return false;
}

short PrepareSpawnFlags(posix_spawn_file_actions_t*)
{
    return 0;
}

void TriggerBusError()
{
    const int fd = ::memfd_create("engine-fault", 0);
    if (fd < 0)
    {
        return;
    }
    // A mapping of a file with no content: the first touch of the page raises SIGBUS.
    void* page = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (page == MAP_FAILED)
    {
        return;
    }
    *static_cast<volatile char*>(page) = 1;
}

void ArmDieWithParent(int)
{
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
}

}  // namespace engine_placeholder::debug::posix

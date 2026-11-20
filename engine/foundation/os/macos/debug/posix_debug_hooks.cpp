// macOS's answers to the shared POSIX death-test backend's hooks (os/posix/debug/posix_debug.h; Phase 21 Day 291).
#include "os/posix/debug/posix_debug.h"

#include "os/last_error.h"
#include "os/signal.h"
#include "sync/thread.h"

#include <crt_externs.h>
#include <fcntl.h>
#include <libproc.h>
#include <spawn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace vse::debug::posix
{

bool MakeCloexecPipe(int fds[2])
{
    // No pipe2 on macOS: the flag is set right after. A fork between the two calls on another thread could inherit the
    // ends; the death tests spawn from one thread, and posix_spawn closes descriptors marked close-on-exec in the child
    // anyway.
    if (::pipe(fds) != 0)
    {
        return false;
    }
    if (::fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 || ::fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0)
    {
        const int error = os::LastError();
        ::close(fds[0]);
        ::close(fds[1]);
        fds[0] = fds[1] = -1;
        os::SetLastError(error);
        return false;
    }
    return true;
}

const char* SelfExecutablePath(char* buffer, usize capacity)
{
    // The kernel's absolute path of the running image (proc_pidpath), not _NSGetExecutablePath's argv-based one, which
    // can be relative (a test that changes directory would then spawn nothing) or a link (Day 291 review).
    return ::proc_pidpath(::getpid(), buffer, static_cast<u32>(capacity)) > 0 ? buffer : nullptr;
}

char** ProcessEnvironment()
{
    return *::_NSGetEnviron();
}

bool ChildNeedsParentWatch()
{
    return true;
}

short PrepareSpawnFlags(posix_spawn_file_actions_t* actions)
{
    // Only the descriptors the file actions name survive the spawn (and standard input, inherited explicitly), so a
    // pipe another thread made between pipe() and fcntl(FD_CLOEXEC) cannot leak into this child (Day 291 review).
    if (::posix_spawn_file_actions_addinherit_np(actions, 0) != 0)
    {
        return -1;
    }
    return POSIX_SPAWN_CLOEXEC_DEFAULT;
}

void TriggerBusError()
{
    void* page = ::mmap(nullptr, static_cast<size_t>(::getpagesize()), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED)
    {
        return;
    }
    *static_cast<volatile char*>(page) = 1;
}

void ArmDieWithParent(int watch_fd)
{
    // macOS has no PR_SET_PDEATHSIG. The parent holds the write end of this pipe and never writes; when it exits or is
    // killed (even with SIGKILL) the kernel closes it and the read below returns 0: the child kills itself, so it never
    // outlives the test program. The thread is detached: it lives as long as the child.
    if (watch_fd < 0)
    {
        return;
    }
    ::fcntl(watch_fd, F_SETFD, FD_CLOEXEC);  // a program the child starts does not inherit the watch
    Thread watcher(
        [watch_fd]
        {
            char byte;
            for (;;)
            {
                const long n = ::read(watch_fd, &byte, 1);
                if (n < 0 && os::LastError() == EINTR)
                {
                    continue;
                }
                if (n == 0)
                {
                    ::kill(::getpid(), SIGKILL);  // end of file: the parent is gone
                }
                return;  // an error (the descriptor was closed under us) is not the parent's death: stop watching, kill
                         // nothing
            }
        });
    watcher.Detach();
}

}  // namespace vse::debug::posix

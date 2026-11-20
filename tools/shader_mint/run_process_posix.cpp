#include <spawn.h>
#include <sys/wait.h>

#include "os/last_error.h"
#include "os/console.h"

#include "run_process.h"

// The process environment: glibc declares it in <unistd.h> with _GNU_SOURCE, macOS nowhere (the same declaration as
// tools/docgen/src/child_process_posix.cpp; it repeats glibc's on Linux).
extern "C" char** environ;

namespace vse::shader_mint
{

bool RunProcess(const Vector<Text>& args)
{
    Vector<char*> argv;
    for (usize i = 0; i < args.Size(); ++i)
    {
        argv.Push(const_cast<char*>(args[i].CStr()));
    }
    argv.Push(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.Data(), environ) != 0)
    {
        os::PrintError("shader_mint: cannot run %s\n", argv[0]);
        return false;
    }
    int status = 0;
    pid_t waited;
    do
    {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && vse::os::LastError() == EINTR);
    if (waited < 0)
    {
        os::PrintError("shader_mint: waiting for %s failed\n", argv[0]);
        return false;
    }
    if (WIFSIGNALED(status))
    {
        os::PrintError("shader_mint: %s was killed by signal %d\n", argv[0], WTERMSIG(status));
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        os::PrintError("shader_mint: %s exited with status %d\n", argv[0],
                       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}

}  // namespace vse::shader_mint

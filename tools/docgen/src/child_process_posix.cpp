// The sample runner's child processes on Linux and macOS (child_process.h): posix_spawn, each child in a process group
// of its own, so a timeout or an interrupt kills the compiler's or the program's whole tree. Moved out of samples.cpp
// on Phase 13 Day 161.
#include "child_process.h"

#include "os/last_error.h"
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace docgen
{

bool StartChild(const Vector<StringView, ArenaAllocator>& args, StringView out_path, StringView err_path,
                StringView working_dir, ArenaAllocator& arena, ChildProcess* out)
{
    Vector<char*, ArenaAllocator> argv(arena);
    for (usize i = 0; i < args.Size(); ++i)
    {
        argv.Push(const_cast<char*>(args[i].Data()));
    }
    argv.Push(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, out_path.Data(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (err_path.Empty())
    {
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
    }
    else
    {
        posix_spawn_file_actions_addopen(&fa, 2, err_path.Data(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    if (!working_dir.Empty())
    {
        posix_spawn_file_actions_addchdir_np(&fa, working_dir.Data());
    }
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);  // own process group: a timeout kills the whole tree
    posix_spawnattr_setpgroup(&attr, 0);
    const bool has_slash = args[0].IndexOf('/') != args[0].Length();
    pid_t pid = -1;
    const int rc = has_slash ? posix_spawn(&pid, args[0].Data(), &fa, &attr, &argv[0], environ)
                             : posix_spawnp(&pid, args[0].Data(), &fa, &attr, &argv[0], environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0)
    {
        return false;
    }
    out->id = pid;
    return true;
}

ChildPoll PollChild(ChildProcess& child, ChildEnd* end)
{
    int status = 0;
    const pid_t pid = static_cast<pid_t>(child.id);
    pid_t r = waitpid(pid, &status, WNOHANG);
    while (r < 0 && engine_placeholder::os::LastError() == EINTR)
    {
        r = waitpid(pid, &status, WNOHANG);
    }
    if (r < 0)
    {
        // The child cannot be waited for (reaped elsewhere, SIGCHLD ignored): never read that as success.
        kill(-pid, SIGKILL);
        child.id = -1;
        return ChildPoll::Lost;
    }
    if (r == 0)
    {
        return ChildPoll::Running;
    }
    *end = ChildEnd{};
    if (WIFEXITED(status))
    {
        end->exited = true;
        end->exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        end->killed = true;
        end->cause = static_cast<u32>(WTERMSIG(status));
    }
    child.id = -1;
    return ChildPoll::Ended;
}

void KillChild(ChildProcess& child)
{
    if (child.id < 0)
    {
        return;
    }
    const pid_t pid = static_cast<pid_t>(child.id);
    kill(-pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && engine_placeholder::os::LastError() == EINTR)
    {
    }
    child.id = -1;
}

}  // namespace docgen

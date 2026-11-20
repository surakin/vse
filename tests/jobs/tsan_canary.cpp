// Deliberately racy program — see tests/jobs/CMakeLists.txt. Built with
// -fsanitize=thread and registered with WILL_FAIL: ThreadSanitizer must
// report the race and make the process exit non-zero. Never "fix" this.
#include "sync/thread.h"

int g_shared = 0;  // plain int, no synchronization: the race

int main()
{
    vse::Thread a(
        []
        {
            for (int i = 0; i < 100000; ++i)
            {
                ++g_shared;
            }
        });
    vse::Thread b(
        []
        {
            for (int i = 0; i < 100000; ++i)
            {
                ++g_shared;
            }
        });
    a.Join();
    b.Join();
    return 0;
}

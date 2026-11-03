// Deliberately racy program — see tests/jobs/CMakeLists.txt. Built with
// -fsanitize=thread and registered with WILL_FAIL: ThreadSanitizer must
// report the race and make the process exit non-zero. Never "fix" this.
#include <thread>

int g_shared = 0; // plain int, no synchronization: the race

int main() {
    std::thread a([] { for (int i = 0; i < 100000; ++i) ++g_shared; });
    std::thread b([] { for (int i = 0; i < 100000; ++i) ++g_shared; });
    a.join();
    b.join();
    return 0;
}

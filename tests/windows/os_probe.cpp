// windows_os_probe: the child program the Windows os tests start (tests/windows/os_windows_test.cpp). One mode per run:
//   args            prints each argument after the mode as hex UTF-8 bytes, one per line ("arg <i> <hex>")
//   log             writes one non-ASCII Error line through the engine's log (to standard output, through os::Write)
//   blocked-stderr  writes 1 MiB to standard error with os::WriteAllWithTimeout (200 ms); the parent never reads that pipe; prints to
//                   standard output how much was written and how long it took
//   blocked-stderr-twice  the same, then a second call at once ("second wrote <n> in <ms> ms")
#include "log/log.h"
#include "os/command_line.h"
#include "os/os.h"

#include <cstdio>
#include <cstring>

using namespace engine_placeholder;

int main(int argc, char** argv) {
    os::StashNativeCommandLine(argc, argv);
    if (argc < 2) return 2;
    if (std::strcmp(argv[1], "args") == 0) {
        int n = 0;
        char** v = nullptr;
        os::Detail_NativeCommandLineArgs(&n, &v);
        for (int i = 2; i < n; ++i) {
            std::printf("arg %d ", i - 2);
            for (const char* c = v[i]; *c != '\0'; ++c) std::printf("%02x", static_cast<unsigned>(static_cast<u8>(*c)));
            std::printf("\n");
        }
        return 0;
    }
    if (std::strcmp(argv[1], "log") == 0) {
        Log(LogCore, LogVerbosity::Error, StringView("probe: h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 \xf0\x9f\x98\x80"));
        return 0;
    }
    if (std::strcmp(argv[1], "blocked-stderr") == 0 || std::strcmp(argv[1], "blocked-stderr-twice") == 0) {
        static char big[1 << 20];
        std::memset(big, 'x', sizeof(big));
        const u64 start = os::MonotonicTimeNanos();
        const long written = os::WriteAllWithTimeout(2, big, sizeof(big), 200ull * 1000 * 1000);
        const u64 ms = (os::MonotonicTimeNanos() - start) / 1000000;
        std::printf("wrote %ld of %zu in %llu ms\n", written, sizeof(big), static_cast<unsigned long long>(ms));
        if (std::strcmp(argv[1], "blocked-stderr-twice") == 0) {
            const u64 again = os::MonotonicTimeNanos();
            const long second = os::WriteAllWithTimeout(2, big, sizeof(big), 200ull * 1000 * 1000);
            std::printf("second wrote %ld in %llu ms\n", second, static_cast<unsigned long long>((os::MonotonicTimeNanos() - again) / 1000000));
        }
        // The helper thread of the timed-out write is still blocked in the pipe; leave without waiting for it (as a crashed process would).
        std::fflush(stdout);
        os::Exit(0);
    }
    return 3;
}

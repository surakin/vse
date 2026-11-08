// The child program of docgen_child_process_windows_test.cpp (Phase 13 Day 161): what docgen's Windows child-process code starts, so the test
// can see what arrived and how it ended.
//
//   child_probe [args...]                 prints each argument on a line of its own, between brackets
//   child_probe --stderr TEXT             writes TEXT to standard error and nothing to standard output
//   child_probe --exit N                  exits with code N
//   child_probe --crash                   faults (an access violation)
//   child_probe --cwd                     prints the working directory
//   child_probe --sleep SECONDS           sleeps
//   child_probe --parent SECONDS          starts `child_probe --sleep SECONDS`, prints its process id, then sleeps as long
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "io/file_system.h"
#include "os/windows/win32.h"

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--stderr") == 0) {
        std::fprintf(stderr, "%s\n", argv[2]);
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--exit") == 0) return std::atoi(argv[2]);
    if (argc >= 2 && std::strcmp(argv[1], "--crash") == 0) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--cwd") == 0) {
        engine_placeholder::io::Path dir;
        if (engine_placeholder::io::CurrentDirectory(&dir).Failed()) return 2;
        std::printf("%.*s\n", static_cast<int>(dir.View().Length()), dir.View().Data());
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--sleep") == 0) {
        Sleep(static_cast<DWORD>(std::atoi(argv[2])) * 1000);
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--parent") == 0) {
        wchar_t self[MAX_PATH + 1];
        GetModuleFileNameW(nullptr, self, MAX_PATH + 1);
        wchar_t command[2 * MAX_PATH];
        std::swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" --sleep %S", self, argv[2]);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 2;
        std::printf("%lu\n", static_cast<unsigned long>(child.dwProcessId));
        std::fflush(stdout);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        Sleep(static_cast<DWORD>(std::atoi(argv[2])) * 1000);
        return 0;
    }
    for (int i = 1; i < argc; ++i) std::printf("[%s]\n", argv[i]);
    return 0;
}

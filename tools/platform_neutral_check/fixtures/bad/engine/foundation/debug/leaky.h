#pragma once
// Every line below is a planted violation, except this comment, which mentions SIGSEGV and ucontext_t harmlessly.
#include "types.h"
#include <unistd.h>
void Capture(const ucontext_t* context);
void Report(siginfo_t* info);
inline long Pid() { pid_t p = getpid(); return p; }
inline const char* Maps() { return "/proc/self/maps"; }
void Wait(HANDLE h);
int Cause() { return SIGSEGV; }
void Spawn() { fork(); }
#include <signal.h>

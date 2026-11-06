#pragma once
// Comments may say SIGSEGV, ucontext_t, pid_t, HANDLE, /proc, fork() and <unistd.h> freely.
/* even in a block comment: SIGABRT waitpid(1) */
#include "types.h"
const char* Describe(); // "SIGSEGV" in a string is fine too: the reason name is data, not an include
inline const char* Name() { return "SIGSEGV"; }
inline int SignedValue() { return 1; } // SIGN-looking text inside a longer word is not SIGxxx: DESIGN_SIGN_X

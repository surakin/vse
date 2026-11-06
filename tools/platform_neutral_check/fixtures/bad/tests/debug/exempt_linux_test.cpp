// Exempt because of the _linux in its name: Linux-only details live here.
#include <signal.h>
#include <unistd.h>
int Number() { return SIGSEGV; }
pid_t Me() { return getpid(); }

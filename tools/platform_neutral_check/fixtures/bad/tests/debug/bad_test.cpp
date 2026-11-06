// Planted: a neutral test must not name signals or wait for processes itself.
int Kind() { return SIGABRT; }
void Reap() { waitpid(-1, 0, 0); }

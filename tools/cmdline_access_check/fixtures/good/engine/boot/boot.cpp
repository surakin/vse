// An entry point that builds the arguments itself is allowed by the allow list.
int First(char** argv) { return argv[0] != nullptr ? 1 : 0; }

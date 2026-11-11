// Points ThreadSanitizer at the database tests' suppressions (tests/database/tsan.supp; the reason is documented there): SQLite's lock-free
// read of its write-ahead-log index header. Only consulted when the binary is built with -fsanitize=thread; harmless otherwise.
extern "C" const char* __tsan_default_options() {
    return "suppressions=" ENGINE_DATABASE_TSAN_SUPP;
}

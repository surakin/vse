#if defined(_WIN32)
int a;
#elif defined(__linux__) && defined(__x86_64__)
int b;
#endif
#if __has_feature(address_sanitizer) // comments such as _MSC_VER are ignored
int c;
#endif

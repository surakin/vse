#include "build_defs.h"
// A comment may name _WIN32 or __linux__, and so may a string: "_MSC_VER"
#if ENGINE_PLATFORM_WINDOWS && !ENGINE_SANITIZER_ADDRESS
int a;
#endif

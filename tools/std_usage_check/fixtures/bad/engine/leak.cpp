#include <vector>
#include "types.h"

// std::string in a comment and "std::vector in a string" are not violations.
void Leak(const char* text) {
    std::sort(text, text);
    unsigned long n = strlen(text);
    memcpy(&n, text, 4);
    ::printf("%s", text);
    unsigned char c = 0;
    (void)c;
}

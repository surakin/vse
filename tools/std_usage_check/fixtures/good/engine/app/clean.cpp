// Uses the wrappers only: Move(x), Format("%d"); std::string and strlen( in comments, "std::vector" and "strlen(x)" in strings.
#include "lang/utility.h"
const char* Text() { return "std::vector strlen(x) #include <vector>"; }
int Member(int* p) { return p->free(1) + p.printf(2) + Other::strlen(3); }

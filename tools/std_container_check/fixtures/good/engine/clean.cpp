// std::vector in a comment, "std::string" in a literal.
#include <atomic>
#include <cstring>
#include <thread>
const char* text = "std::vector<int>";
std::atomic<int> counter;
void Copy(char* a, const char* b) { std::memcpy(a, b, 4); }

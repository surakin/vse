#include <vector>
#include <thread>
// std::string in a comment is fine
const char* s = "std::string in a literal is fine";
std::vector<int> a;
std::unique_ptr<int> b;
std::string c;
unsigned char d;
std::uint32_t e;
int f = std::min(1, 2);
void g() { std::sort(a.begin(), a.end()); }
std::thread t; // allowed
std::string_view v;

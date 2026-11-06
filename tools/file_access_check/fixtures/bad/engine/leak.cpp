#include <fstream>
void Violations() {
    FILE* a = fopen("x", "rb");
    int b = open("x", 0);
    DIR* c = opendir(".");
    std::ifstream d("x");
    mkdir("d", 0755);
    std::rename("a", "b");
    getcwd(nullptr, 0);
    struct stat s;
    stat("x", &s);
    ::unlink("x");
}

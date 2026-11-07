#include <cstdlib>
// getenv("IN_A_COMMENT") is fine
void F(int argc, char** argv) {
    const char* a = std::getenv("MY_OPTION");
    const char* b = getenv("OTHER");
    if (argc > 1 && argv[1][0] == '-') { /* "--in-a-block-comment" */ }
    if (std::strcmp(argv[1], "--sneaky") == 0) {}
    const char* c = obj.getenv("member");   // a member call is not the C function... but the check is conservative about names
}
const char* url = "http://x"; const char* z = getenv("AFTER_URL");

#include "os.h"

int main() {
    const char msg[] = "OK\n";
    engine_placeholder::os::Write(1, msg, sizeof(msg) - 1);
    return 0;
}

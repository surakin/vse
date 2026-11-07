#include "platform/window_host.h"

int main(int argc, char** argv) { return argc > 0 && argv != nullptr ? 0 : 1; }
extern "C" int __stdcall wWinMain(struct HINSTANCE__*, struct HINSTANCE__*, wchar_t*, int) { return 0; }

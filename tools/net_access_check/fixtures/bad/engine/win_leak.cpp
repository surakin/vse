#include <winsock2.h>
int Start(void* d) { return WSAStartup(0x0202, d); }
int Stop(unsigned long long s) { return closesocket(s); }
int Wait(void* r) { return select(0, r, nullptr, nullptr, nullptr) + FreeAddrInfoW(nullptr); }

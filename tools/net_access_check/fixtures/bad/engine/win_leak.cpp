#include <winsock2.h>
int Start(void* d) { return WSAStartup(0x0202, d); }
int Stop(unsigned long long s) { return closesocket(s); }

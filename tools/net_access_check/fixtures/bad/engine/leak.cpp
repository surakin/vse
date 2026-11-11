#include <sys/socket.h>
int Open() { return ::socket(2, 1, 0); }
int Bind(int s, const void* a, unsigned n) {
    return bind(s, a, n);
}
int Link(int s) { return ::listen(s, 1) + accept(s, 0, 0); }
long Out(int s, const char* p, unsigned long n) { return send(s, p, n, 0) + ::recv(s, 0, n, 0); }
void Setup(int s, int* v) { setsockopt(s, 1, 2, v, 4); getaddrinfo(0, 0, 0, 0); }
int Poll() { return epoll_create1(0); }

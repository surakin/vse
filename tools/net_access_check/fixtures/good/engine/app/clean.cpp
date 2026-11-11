// Comments, strings, member calls and our own names are not violations: socket( bind( #include <sys/socket.h>
#include "net/socket.h"
template <typename Queue>
void Run(Queue& q, Queue* p) {
    q.send(1);
    p->send(2);
    const char* text = "connect(listen(send(";
    (void)text;
    /* recv(
       bind( */
    Socket s; s.Bind(0); s.Connect(0); s.Send(0);
}
// SIGNAL-SAFE-BEGIN
void InCrash(int fd) { send(fd, 0, 0, 0); }
// SIGNAL-SAFE-END

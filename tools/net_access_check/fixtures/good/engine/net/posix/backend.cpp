// The backend directory may name the whole API.
#include <sys/socket.h>
#include <netinet/in.h>
int Open() { return ::socket(AF_INET, SOCK_STREAM, 0); }

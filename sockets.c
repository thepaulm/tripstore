#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "sockets.h"

int
sock_connect(const char *hostname, int port)
{
    int s;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(hostname);
    if (addr.sin_addr.s_addr == -1) {
        struct hostent *phe;
        phe = gethostbyname(hostname);
        if (!phe)
            return -1;
        addr.sin_addr.s_addr = ((struct in_addr*)(phe->h_addr))->s_addr;
    }

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    if (-1 == connect(s, (struct sockaddr*)&addr, sizeof(addr))) {
        close(s);
        return -1;
    }

    return s;
}

int
listen_on_port(int port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (-1 == bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
        close(s);
        return -1;
    }
    if (-1 == listen(s, SOMAXCONN)) {
        close(s);
        return -1;
    }
    return s;
}

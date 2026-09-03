#pragma once

#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET socket_t;
#define Q3_INVALID_SOCKET INVALID_SOCKET
#define q3_closesocket closesocket
#define q3_sockerrno() WSAGetLastError()

inline int q3_set_nonblocking(socket_t s) {
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one);
}
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>

typedef int socket_t;
#define Q3_INVALID_SOCKET (-1)
#define q3_closesocket close
#define q3_sockerrno() errno

inline int q3_set_nonblocking(socket_t s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
#endif

#endif // NET_COMPAT_H

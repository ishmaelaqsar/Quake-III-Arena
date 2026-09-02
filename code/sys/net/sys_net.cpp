#include "net_compat.h"
#include "../sys_local.h"
#include <string.h>

extern "C" {

netadr_t net_local_adr;
socket_t ip_socket = Q3_INVALID_SOCKET;
static socket_t ipx_socket = Q3_INVALID_SOCKET;

static cvar_t *noudp;

#define MAX_IPS 16
static int numIP;
static byte localIP[MAX_IPS][4];

socket_t NET_IPSocket(char *net_interface, int port);
char *NET_ErrorString(void);

void NetadrToSockadr(netadr_t *a, struct sockaddr_in *s) {
    memset(s, 0, sizeof(*s));

    if (a->type == NA_BROADCAST) {
        s->sin_family = AF_INET;
        s->sin_port = a->port;
        s->sin_addr.s_addr = INADDR_BROADCAST;
    } else if (a->type == NA_IP) {
        s->sin_family = AF_INET;
        memcpy(&s->sin_addr, a->ip, sizeof(s->sin_addr));
        s->sin_port = a->port;
    }
}

void SockadrToNetadr(struct sockaddr_in *s, netadr_t *a) {
    memcpy(a->ip, &s->sin_addr, sizeof(a->ip));
    a->port = s->sin_port;
    a->type = NA_IP;
}

char *NET_BaseAdrToString(netadr_t a) {
    static char s[64];
    Com_sprintf(s, sizeof(s), "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
    return s;
}

qboolean Sys_StringToSockaddr(const char *s, struct sockaddr *sadr) {
    struct hostent *h;

    memset(sadr, 0, sizeof(*sadr));
    struct sockaddr_in *sadr_in = (struct sockaddr_in *)sadr;
    sadr_in->sin_family = AF_INET;
    sadr_in->sin_port = 0;

    if (s[0] >= '0' && s[0] <= '9') {
        sadr_in->sin_addr.s_addr = inet_addr(s);
    } else {
        if (!(h = gethostbyname(s))) {
            return qfalse;
        }
        memcpy(&sadr_in->sin_addr, h->h_addr_list[0], sizeof(sadr_in->sin_addr));
    }

    return qtrue;
}

qboolean Sys_StringToAdr(const char *s, netadr_t *a) {
    struct sockaddr_in sadr;

    if (!Sys_StringToSockaddr(s, (struct sockaddr *)&sadr)) {
        return qfalse;
    }

    SockadrToNetadr(&sadr, a);
    return qtrue;
}

qboolean Sys_GetPacket(netadr_t *net_from, msg_t *net_message) {
    int ret;
    struct sockaddr_in from;
    socklen_t fromlen;
    socket_t net_socket;
    int protocol;
    int err;

    for (protocol = 0; protocol < 2; protocol++) {
        if (protocol == 0) {
            net_socket = ip_socket;
        } else {
            net_socket = ipx_socket;
        }

        if (net_socket == Q3_INVALID_SOCKET) {
            continue;
        }

        fromlen = sizeof(from);
        ret = recvfrom(net_socket, (char *)net_message->data, net_message->maxsize,
                       0, (struct sockaddr *)&from, &fromlen);

        SockadrToNetadr(&from, net_from);
        net_message->readcount = 0;

        if (ret == -1) {
            err = q3_sockerrno();
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK || err == WSAECONNREFUSED) {
                continue;
            }
#else
            if (err == EWOULDBLOCK || err == ECONNREFUSED) {
                continue;
            }
#endif
            Com_Printf("NET_GetPacket: %s from %s\n", NET_ErrorString(),
                       NET_AdrToString(*net_from));
            continue;
        }

        if (ret == net_message->maxsize) {
            Com_Printf("Oversize packet from %s\n", NET_AdrToString(*net_from));
            continue;
        }

        net_message->cursize = ret;
        return qtrue;
    }

    return qfalse;
}

void Sys_SendPacket(int length, const void *data, netadr_t to) {
    int ret;
    struct sockaddr_in addr;
    socket_t net_socket;

    if (to.type == NA_BROADCAST || to.type == NA_IP) {
        net_socket = ip_socket;
    } else if (to.type == NA_IPX || to.type == NA_BROADCAST_IPX) {
        net_socket = ipx_socket;
    } else {
        Com_Error(ERR_FATAL, "NET_SendPacket: bad address type");
        return;
    }

    if (net_socket == Q3_INVALID_SOCKET) {
        return;
    }

    NetadrToSockadr(&to, &addr);

    ret = sendto(net_socket, (const char *)data, length, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        Com_Printf("NET_SendPacket ERROR: %s to %s\n", NET_ErrorString(),
                   NET_AdrToString(to));
    }
}

qboolean Sys_IsLANAddress(netadr_t adr) {
    int i;

    if (adr.type == NA_LOOPBACK || adr.type == NA_IPX) {
        return qtrue;
    }

    if (adr.type != NA_IP) {
        return qfalse;
    }

    if ((adr.ip[0] & 0x80) == 0x00) {
        for (i = 0; i < numIP; i++) {
            if (adr.ip[0] == localIP[i][0]) {
                return qtrue;
            }
        }
        return qfalse;
    }

    if ((adr.ip[0] & 0xc0) == 0x80) {
        for (i = 0; i < numIP; i++) {
            if (adr.ip[0] == localIP[i][0] && adr.ip[1] == localIP[i][1]) {
                return qtrue;
            }
            if (adr.ip[0] == 172 && localIP[i][0] == 172 && (adr.ip[1] & 0xf0) == 16 && (localIP[i][1] & 0xf0) == 16) {
                return qtrue;
            }
        }
        return qfalse;
    }

    for (i = 0; i < numIP; i++) {
        if (adr.ip[0] == localIP[i][0] && adr.ip[1] == localIP[i][1] && adr.ip[2] == localIP[i][2]) {
            return qtrue;
        }
        if (adr.ip[0] == 192 && localIP[i][0] == 192 && adr.ip[1] == 168 && localIP[i][1] == 168) {
            return qtrue;
        }
    }
    return qfalse;
}

void Sys_ShowIP(void) {
    int i;
    for (i = 0; i < numIP; i++) {
        Com_Printf("IP: %i.%i.%i.%i\n", localIP[i][0], localIP[i][1], localIP[i][2], localIP[i][3]);
    }
}

void NET_GetLocalAddress(void) {
    char hostname[256];
    struct hostent *hostInfo;
    char *p;
    int ip;
    int n;

    if (gethostname(hostname, 256) == -1) {
        return;
    }

    hostInfo = gethostbyname(hostname);
    if (!hostInfo) {
        return;
    }

    Com_Printf("Hostname: %s\n", hostInfo->h_name);
    n = 0;
    while ((p = hostInfo->h_aliases[n++]) != NULL) {
        Com_Printf("Alias: %s\n", p);
    }

    if (hostInfo->h_addrtype != AF_INET) {
        return;
    }

    numIP = 0;
    while ((p = hostInfo->h_addr_list[numIP]) != NULL && numIP < MAX_IPS) {
        ip = ntohl(*(int *)p);
        localIP[numIP][0] = p[0];
        localIP[numIP][1] = p[1];
        localIP[numIP][2] = p[2];
        localIP[numIP][3] = p[3];
        Com_Printf("IP: %i.%i.%i.%i\n", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
        numIP++;
    }
}

void NET_OpenIP(void) {
    cvar_t *ip;
    int port;
    int i;

    ip = Cvar_Get("net_ip", "localhost", 0);
    port = Cvar_Get("net_port", va("%i", PORT_SERVER), 0)->value;

    for (i = 0; i < 10; i++) {
        ip_socket = NET_IPSocket(ip->string, port + i);
        if (ip_socket != Q3_INVALID_SOCKET) {
            Cvar_SetValue("net_port", port + i);
            NET_GetLocalAddress();
            return;
        }
    }
    Com_Error(ERR_FATAL, "Couldn't allocate IP port");
}

void NET_Init(void) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    noudp = Cvar_Get("net_noudp", "0", 0);
    if (!noudp->value) {
        NET_OpenIP();
    }
}

socket_t NET_IPSocket(char *net_interface, int port) {
    socket_t newsocket;
    struct sockaddr_in address;
    int i = 1;

    if (net_interface) {
        Com_Printf("Opening IP socket: %s:%i\n", net_interface, port);
    } else {
        Com_Printf("Opening IP socket: localhost:%i\n", port);
    }

    newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (newsocket == Q3_INVALID_SOCKET) {
        Com_Printf("ERROR: UDP_OpenSocket: socket: %s", NET_ErrorString());
        return Q3_INVALID_SOCKET;
    }

    if (q3_set_nonblocking(newsocket) == -1) {
        Com_Printf("ERROR: UDP_OpenSocket: nonblocking:%s\n", NET_ErrorString());
        q3_closesocket(newsocket);
        return Q3_INVALID_SOCKET;
    }

    if (setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i)) == -1) {
        Com_Printf("ERROR: UDP_OpenSocket: setsockopt SO_BROADCAST:%s\n", NET_ErrorString());
        q3_closesocket(newsocket);
        return Q3_INVALID_SOCKET;
    }

    memset(&address, 0, sizeof(address));
    if (!net_interface || !net_interface[0] || !Q_stricmp(net_interface, "localhost")) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        Sys_StringToSockaddr(net_interface, (struct sockaddr *)&address);
    }

    if (port == PORT_ANY) {
        address.sin_port = 0;
    } else {
        address.sin_port = htons((short)port);
    }

    address.sin_family = AF_INET;

    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        Com_Printf("ERROR: UDP_OpenSocket: bind: %s\n", NET_ErrorString());
        q3_closesocket(newsocket);
        return Q3_INVALID_SOCKET;
    }

    return newsocket;
}

void NET_Shutdown(void) {
    if (ip_socket != Q3_INVALID_SOCKET) {
        q3_closesocket(ip_socket);
        ip_socket = Q3_INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

char *NET_ErrorString(void) {
#ifdef _WIN32
    static char win_err_buf[256];
    DWORD err = WSAGetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   win_err_buf, sizeof(win_err_buf), NULL);
    return win_err_buf;
#else
    return strerror(errno);
#endif
}

void NET_Sleep(int msec) {
    struct timeval timeout;
    fd_set fdset;
    extern qboolean stdin_active;

    if (ip_socket == Q3_INVALID_SOCKET || !com_dedicated || !com_dedicated->integer) {
        return;
    }

    FD_ZERO(&fdset);
    if (stdin_active) {
        FD_SET(0, &fdset);
    }
    FD_SET(ip_socket, &fdset);
    timeout.tv_sec = msec / 1000;
    timeout.tv_usec = (msec % 1000) * 1000;
#ifdef _WIN32
    select(0, &fdset, NULL, NULL, &timeout);
#else
    select(ip_socket + 1, &fdset, NULL, NULL, &timeout);
#endif
}

} // extern "C"

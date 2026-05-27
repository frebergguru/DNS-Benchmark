#ifndef DNSB_SOCKET_COMPAT_H
#define DNSB_SOCKET_COMPAT_H

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET dnsb_sock;
  #define DNSB_INVALID_SOCK INVALID_SOCKET
  #define dnsb_closesock closesocket
  #define dnsb_sockerr() WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  typedef int dnsb_sock;
  #define DNSB_INVALID_SOCK (-1)
  #define dnsb_closesock close
  #define dnsb_sockerr() errno
#endif

#include <stddef.h>

int  dnsb_net_init(void);
void dnsb_net_shutdown(void);

int  dnsb_set_nonblocking(dnsb_sock s);
int  dnsb_parse_addr(const char *addr, int port, struct sockaddr_storage *out, socklen_t *outlen);

#endif

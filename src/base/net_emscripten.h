#ifndef BASE_NET_EMSCRIPTEN_H
#define BASE_NET_EMSCRIPTEN_H

#include "net.h"

#if defined(CONF_PLATFORM_EMSCRIPTEN)
struct CWebNetSocket;

CWebNetSocket *web_net_create(unsigned short Port);
void web_net_close(CWebNetSocket *pSocket);
int web_net_send(CWebNetSocket *pSocket, const NETADDR *pAddr, const void *pData, int Size);
int web_net_recv(CWebNetSocket *pSocket, NETADDR *pAddr, unsigned char **ppData);
int web_net_wait(CWebNetSocket *pSocket, std::chrono::nanoseconds Timeout);
#endif

#endif

#include "net_emscripten.h"

#if defined(CONF_PLATFORM_EMSCRIPTEN)
#include "mem.h"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

namespace
{
	constexpr int MAX_PACKET_SIZE = 1400;
	constexpr size_t MAX_QUEUED_PACKETS = 256;

	struct CPacket
	{
		unsigned short m_SourcePort;
		int m_Size;
		std::array<unsigned char, MAX_PACKET_SIZE> m_aData;
	};

	std::mutex s_Mutex;
	std::map<unsigned short, CWebNetSocket *> s_Sockets;
	unsigned short s_NextPort = 49152;
}

struct CWebNetSocket
{
	unsigned short m_Port;
	std::deque<CPacket> m_Queue;
	std::condition_variable m_Received;
	std::array<unsigned char, MAX_PACKET_SIZE> m_aBuffer;
};

extern "C" EMSCRIPTEN_KEEPALIVE void EmscriptenLocalPacket(int SourcePort, int DestinationPort, const unsigned char *pData, int Size)
{
	if(SourcePort <= 0 || SourcePort > 65535 || DestinationPort <= 0 || DestinationPort > 65535 || Size <= 0 || Size > MAX_PACKET_SIZE)
		return;
	std::lock_guard<std::mutex> Lock(s_Mutex);
	const auto It = s_Sockets.find(DestinationPort);
	if(It == s_Sockets.end() || It->second->m_Queue.size() >= MAX_QUEUED_PACKETS)
		return;
	CPacket Packet;
	Packet.m_SourcePort = SourcePort;
	Packet.m_Size = Size;
	mem_copy(Packet.m_aData.data(), pData, Size);
	It->second->m_Queue.push_back(Packet);
	It->second->m_Received.notify_one();
}

extern "C" EMSCRIPTEN_KEEPALIVE void EmscriptenLocalReset()
{
	std::lock_guard<std::mutex> Lock(s_Mutex);
	for(auto &[Port, pSocket] : s_Sockets)
		pSocket->m_Queue.clear();
}

CWebNetSocket *web_net_create(unsigned short Port)
{
	std::lock_guard<std::mutex> Lock(s_Mutex);
	if(Port == 0)
	{
		for(int Attempt = 0; Attempt < 16384; ++Attempt)
		{
			Port = s_NextPort;
			s_NextPort = s_NextPort == 65535 ? 49152 : s_NextPort + 1;
			if(s_Sockets.find(Port) == s_Sockets.end())
				break;
		}
	}
	if(s_Sockets.find(Port) != s_Sockets.end())
		return nullptr;
	auto *pSocket = new CWebNetSocket;
	pSocket->m_Port = Port;
	s_Sockets.emplace(Port, pSocket);
	return pSocket;
}

void web_net_close(CWebNetSocket *pSocket)
{
	std::lock_guard<std::mutex> Lock(s_Mutex);
	s_Sockets.erase(pSocket->m_Port);
	delete pSocket;
}

int web_net_send(CWebNetSocket *pSocket, const NETADDR *pAddr, const void *pData, int Size)
{
	static const unsigned char aLoopback[16] = {127, 0, 0, 1};
	if(!(pAddr->type & NETTYPE_IPV4) || mem_comp(pAddr->ip, aLoopback, sizeof(aLoopback)) != 0 || pAddr->port == 0 || Size <= 0 || Size > MAX_PACKET_SIZE)
		return -1;
	// Copy before returning: the caller owns pData, and server sends originate on a pthread.
	MAIN_THREAD_EM_ASM({
		if(Module['localNetworkSend'])
			Module['localNetworkSend']($0, $1, HEAPU8.slice($2, $2 + $3)); }, pSocket->m_Port, pAddr->port, pData, Size);
	return Size;
}

int web_net_recv(CWebNetSocket *pSocket, NETADDR *pAddr, unsigned char **ppData)
{
	std::lock_guard<std::mutex> Lock(s_Mutex);
	if(pSocket->m_Queue.empty())
		return 0;
	const CPacket &Packet = pSocket->m_Queue.front();
	*pAddr = NETADDR_ZEROED;
	pAddr->type = NETTYPE_IPV4;
	pAddr->ip[0] = 127;
	pAddr->ip[3] = 1;
	pAddr->port = Packet.m_SourcePort;
	const int Size = Packet.m_Size;
	mem_copy(pSocket->m_aBuffer.data(), Packet.m_aData.data(), Size);
	*ppData = pSocket->m_aBuffer.data();
	pSocket->m_Queue.pop_front();
	return Size;
}

int web_net_wait(CWebNetSocket *pSocket, std::chrono::nanoseconds Timeout)
{
	std::unique_lock<std::mutex> Lock(s_Mutex);
	// The browser/UI thread must keep processing incoming Worker messages.
	if(!emscripten_is_main_runtime_thread())
		pSocket->m_Received.wait_for(Lock, Timeout, [&] { return !pSocket->m_Queue.empty(); });
	return !pSocket->m_Queue.empty();
}
#endif

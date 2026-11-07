// Standalone Emscripten transport test; see BUILDING-emscripten.md for the command.
#include <base/net_emscripten.h>

#include <emscripten/emscripten.h>

#include <cassert>
#include <cstdio>
#include <thread>

const NETADDR NETADDR_ZEROED = {};
extern "C" void EmscriptenLocalPacket(int Source, int Destination, const unsigned char *pData, int Size);
extern "C" void EmscriptenLocalReset();

int main()
{
	MAIN_THREAD_EM_ASM({
		Module['localNetworkSend'] = (source, destination, data) => {
			Module.ccall('EmscriptenLocalPacket', null, ['number', 'number', 'array', 'number'], [source, destination, data, data.length]);
}
});
auto *pServer = web_net_create(8303);
auto *pPlayer = web_net_create(49152);
auto *pDummy = web_net_create(49153);
assert(pServer &&pPlayer &&pDummy);
assert(!web_net_create(8303));
NETADDR Address = {};
Address.type = NETTYPE_IPV4;
Address.ip[0] = 127;
Address.ip[3] = 1;
Address.port = 8303;
unsigned char aData[] = {1, 2, 3};
unsigned char *pReceived;
NETADDR Source;
for(auto *pClient : {pPlayer, pDummy})
{
	assert(web_net_send(pClient, &Address, aData, sizeof(aData)) == 3);
	assert(web_net_recv(pServer, &Source, &pReceived) == 3);
	assert(Source.port == (pClient == pPlayer ? 49152 : 49153));
	assert(pReceived[0] == 1 && pReceived[2] == 3);
	assert(web_net_send(pServer, &Source, aData, sizeof(aData)) == 3);
	assert(web_net_recv(pClient, &Source, &pReceived) == 3);
	assert(Source.port == 8303);
}
Address.ip[0] = 8;
assert(web_net_send(pPlayer, &Address, aData, sizeof(aData)) == -1);
Address.ip[0] = 127;
assert(web_net_send(pPlayer, &Address, aData, 1401) == -1);
EmscriptenLocalPacket(49152, 8303, aData, 1401);
EmscriptenLocalPacket(49152, 8303, aData, -1);
EmscriptenLocalPacket(0, 8303, aData, 3);
EmscriptenLocalPacket(49152, 9999, aData, 3);
assert(web_net_recv(pServer, &Source, &pReceived) == 0);
for(int i = 0; i < 300; ++i)
	EmscriptenLocalPacket(49152, 8303, aData, 3);
int Count = 0;
while(web_net_recv(pServer, &Source, &pReceived))
	++Count;
assert(Count == 256);
EmscriptenLocalPacket(49152, 8303, aData, 3);
EmscriptenLocalReset();
assert(web_net_recv(pServer, &Source, &pReceived) == 0);
bool Woken = false;
std::thread Waiting([&] { Woken = web_net_wait(pServer, std::chrono::seconds(2)); });
EmscriptenLocalPacket(49152, 8303, aData, 3);
Waiting.join();
assert(Woken);
web_net_close(pPlayer);
pPlayer = web_net_create(49152);
assert(pPlayer &&web_net_recv(pPlayer, &Source, &pReceived) == 0);
web_net_close(pPlayer);
web_net_close(pDummy);
web_net_close(pServer);
std::puts("Local network: routing, bounds, overflow, reset, close and wakeup passed.");
return 0;
}

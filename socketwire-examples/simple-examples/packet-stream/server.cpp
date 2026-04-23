#include "i_socket.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace socketwire; // NOLINT

int main()
{
  initialize_sockets();
  auto* factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->createUDPSocket(SocketConfig{});
  if (socket == nullptr || socket->bind(SocketConstants::any(), 53473) != SocketError::None)
  {
    printf("Cannot create server socket\n");
    return 1;
  }

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client)
  {
    printf("Connection with %u:%u established\n", client.address.ipv4.hostOrderAddress, client.port);
  });
  hub.setPacketCallback([](auto&, std::uint8_t, const void* data, std::size_t size, bool)
  {
    printf("Packet received '%.*s'\n", static_cast<int>(size), static_cast<const char*>(data));
  });

  printf("packet-stream server listening on port 53473\n");
  while (true)
  {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

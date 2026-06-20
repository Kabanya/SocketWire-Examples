#include <chrono>
#include <cstdio>
#include <print>
#include <thread>

#include "i_socket.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_PACKET_STREAM_PORT", 53473);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Cannot init SocketWire");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(socket_constants::Any(), port) != SocketError::kNone) {
    std::println("Cannot create server socket");
    return 1;
  }

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.SetConnectedCallback([](auto& client) {
    printf("Connection with %u:%u established\n",
           client.address.ipv4.hostOrderAddress, client.port);
  });
  hub.SetPacketCallback(
    [](auto&, std::uint8_t, const void* data, std::size_t size, bool) {
      std::println("Packet received '{:.{}}'", static_cast<const char*>(data),
                   static_cast<int>(size));
    });

  std::println("packet-stream server listening on port {}",
               static_cast<unsigned>(port));
  while (true) {
    hub.Poll();
    hub.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

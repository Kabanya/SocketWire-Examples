#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

void DrainSocket(ISocket& socket) {
  while (true) {
    std::uint8_t buffer[4096]{};
    SocketAddress from{};
    std::uint16_t from_port = 0;
    const auto result = socket.Receive(buffer, sizeof(buffer), from, from_port);
    if (result.error == SocketError::kWouldBlock) return;
    if (result.Failed()) {
      std::cerr << "Socket error: " << ToString(result.error) << '\n';
      return;
    }
    if (result.bytes <= 0) return;

    std::cout << "Received: "
              << std::string(reinterpret_cast<const char*>(buffer),
                             static_cast<std::size_t>(result.bytes))
              << '\n';
  }
}

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_ECHO_PORT", 40404);

  socketwire::InitializeSockets();

  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::cout << "Socket factory not initialized\n";
    return 1;
  }

  auto server = factory->CreateUdpSocket(SocketConfig{});
  if (!server) {
    std::cout << "Cannot create socket\n";
    return 1;
  }

  SocketAddress const bind_addr = SocketConstants::Any();
  if (server->Bind(bind_addr, port) != SocketError::kNone) {
    std::cout << "Bind failed\n";
    return 1;
  }
  std::cout << "Server listening on port " << port << "\n";
  while (true) {
    DrainSocket(*server);
    std::this_thread::sleep_for(std::chrono::microseconds(10000));
  }
}

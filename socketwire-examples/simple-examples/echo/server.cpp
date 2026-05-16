#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

class PrintHandler : public ISocketEventHandler {
 public:
  void OnDataReceived([[maybe_unused]] const SocketAddress& from,
                      [[maybe_unused]] std::uint16_t from_port,
                      const void* data, std::size_t bytes_read) override {
    std::cout << "Received: "
              << std::string(static_cast<const char*>(data), bytes_read)
              << '\n';
  }
  void OnSocketError(SocketError error_code) override {
    std::cerr << "Socket error: " << static_cast<int>(error_code) << '\n';
  }
};

// Forward declaration from posix_udp_socket.cpp
namespace socketwire {
extern void RegisterPosixSocketFactory();
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

  PrintHandler handler;

  SocketAddress const bind_addr = SocketConstants::Any();
  if (server->Bind(bind_addr, port) != SocketError::kNone) {
    std::cout << "Bind failed\n";
    return 1;
  }
  std::cout << "Server listening on port " << port << "\n";
  while (true) {
    server->Poll(&handler);
    std::this_thread::sleep_for(std::chrono::microseconds(10000));
  }
}

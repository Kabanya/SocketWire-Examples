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

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_ECHO_PORT", 40404);

  // Initialize socket factory
  InitializeSockets();

  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::cout << "Socket factory not initialized\n";
    return 1;
  }

  auto client = factory->CreateUdpSocket(SocketConfig{});
  if (!client) {
    std::cout << "Cannot create socket\n";
    return 1;
  }

  PrintHandler handler;

  SocketAddress const bind_addr = SocketConstants::Any();
  client->Bind(bind_addr, 0);

  SocketAddress const dest = SocketConstants::Loopback();

  std::string const msg = "Hello from use case of Client from Socket class!";
  client->SendTo(msg.c_str(), msg.size(), dest, port);

  for (int i = 0; i < 10; ++i) {
    client->Poll(&handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "echo client is finished" << '\n';
  return 0;
}

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
                      [[maybe_unused]] std::uint16_t fromPort, const void* data,
                      std::size_t bytesRead) override {
    std::cout << "Received: "
              << std::string(static_cast<const char*>(data), bytesRead)
              << std::endl;
  }
  void OnSocketError(SocketError errorCode) override {
    std::cerr << "Socket error: " << static_cast<int>(errorCode) << std::endl;
  }
};

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
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

  SocketAddress bindAddr = SocketConstants::Any();
  client->Bind(bindAddr, 0);

  SocketAddress dest = SocketConstants::Loopback();

  std::string msg = "Hello from use case of Client from Socket class!";
  client->SendTo(msg.c_str(), msg.size(), dest, port);

  for (int i = 0; i < 10; ++i) {
    client->Poll(&handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "echo client is finished" << std::endl;
  return 0;
}

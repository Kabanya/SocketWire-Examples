#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

#include "i_socket.hpp"
#include "socket_init.hpp"
#include "socket_constants.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire; //NOLINT

class PrintHandler : public ISocketEventHandler
{
public:
  void OnDataReceived([[maybe_unused]]const SocketAddress& from, [[maybe_unused]]std::uint16_t fromPort,
                      const void* data, std::size_t bytesRead) override
  {
    std::cout << "Received: " << std::string(static_cast<const char*>(data), bytesRead) << std::endl;
  }
  void OnSocketError(SocketError errorCode) override
  {
    std::cerr << "Socket error: " << static_cast<int>(errorCode) << std::endl;
  }
};

// Forward declaration from posix_udp_socket.cpp
namespace socketwire {
extern void register_posix_socket_factory();
}

int main(int argc, const char** argv)
{
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_ECHO_PORT", 40404);

  socketwire::InitializeSockets();

  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr)
  {
    std::cout << "Socket factory not initialized\n";
    return 1;
  }

  auto server = factory->CreateUdpSocket(SocketConfig{});
  if (!server)
  {
    std::cout << "Cannot create socket\n";
    return 1;
  }

  PrintHandler handler;

  SocketAddress bindAddr = SocketConstants::Any();
  if (server->Bind(bindAddr, port) != SocketError::kNone)
  {
    std::cout << "Bind failed\n";
    return 1;
  }
  std::cout << "Server listening on port " << port << "\n";
  while (true)
  {
    server->Poll(&handler);
    std::this_thread::sleep_for(std::chrono::microseconds(10000));
  }
}

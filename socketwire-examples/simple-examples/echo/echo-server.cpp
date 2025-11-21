#include "i_socket.hpp"
#include "socket_init.hpp"
#include "socket_constants.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

using namespace socketwire; //NOLINT

class PrintHandler : public ISocketEventHandler
{
public:
  void onDataReceived([[maybe_unused]]const SocketAddress& from, [[maybe_unused]]std::uint16_t fromPort,
                      const void* data, std::size_t bytesRead) override
  {
    std::cout << "Received: " << std::string(static_cast<const char*>(data), bytesRead) << std::endl;
  }
  void onSocketError(SocketError errorCode) override
  {
    std::cerr << "Socket error: " << static_cast<int>(errorCode) << std::endl;
  }
};

// Forward declaration from posix_udp_socket.cpp
namespace socketwire {
extern void register_posix_socket_factory();
}

int main()
{
  socketwire::initialize_sockets();

  auto factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::cout << "Socket factory not initialized\n";
    return 1;
  }

  auto server = factory->createUDPSocket(SocketConfig{});
  if (!server)
  {
    std::cout << "Cannot create socket\n";
    return 1;
  }

  PrintHandler handler;

  SocketAddress bindAddr = SocketConstants::any();
  if (server->bind(bindAddr, 40404) != SocketError::None)
  {
    std::cout << "Bind failed\n";
    return 1;
  }
  std::cout << "Server listening on port 40404\n";
  while (true)
  {
    server->poll(&handler);
    std::this_thread::sleep_for(std::chrono::microseconds(10000));
  }
}

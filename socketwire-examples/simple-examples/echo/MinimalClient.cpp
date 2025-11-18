#include "i_socket.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <netinet/in.h>

using namespace socketwire; //NOLINT

// Forward declaration from posix_udp_socket.cpp
namespace socketwire {
extern void register_posix_socket_factory();
}

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

int main()
{
  // Initialize socket factory
  register_posix_socket_factory();

  auto factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::cout << "Socket factory not initialized\n";
    return 1;
  }

  auto client = factory->createUDPSocket(SocketConfig{});
  if (!client)
  {
    std::cout << "Cannot create socket\n";
    return 1;
  }

  PrintHandler handler;

  SocketAddress bindAddr = SocketAddress::fromIPv4(INADDR_ANY);
  client->bind(bindAddr, 0);

  SocketAddress dest = SocketAddress::fromIPv4(INADDR_LOOPBACK);

  std::string msg = "Hello from use case of Client from Socket class!";
  client->sendTo(msg.c_str(), msg.size(), dest, 40404);

  for (int i = 0; i < 10; ++i)
  {
    client->poll(&handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "echo client is finished" << std::endl;
  return 0;
}

#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace socketwire; // NOLINT

class ClientHandler final : public IReliableConnectionHandler
{
public:
  void onConnected() override
  {
    connected = true;
    printf("Connection established\n");
  }

  void onDisconnected() override { connected = false; }

  void onReliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    printf("Packet received '%.*s'\n", static_cast<int>(size), static_cast<const char*>(data));
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    onReliableReceived(channel, data, size);
  }

  bool connected = false;
};

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
  if (socket == nullptr || socket->bind(SocketConstants::any(), 0) != SocketError::None)
  {
    printf("Cannot create client socket\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(SocketConstants::loopback(), 53473);

  auto lastSend = std::chrono::steady_clock::now();
  int counter = 0;

  while (true)
  {
    connection.tick();
    if (handler.connected)
    {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() > 100)
      {
        lastSend = now;
        std::string packet = "packet#" + std::to_string(counter++);
        connection.sendReliable(1, packet.c_str(), packet.size() + 1);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

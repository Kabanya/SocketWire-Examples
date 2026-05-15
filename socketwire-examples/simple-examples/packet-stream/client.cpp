#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace socketwire; // NOLINT

class ClientHandler final : public IReliableConnectionHandler
{
public:
  void OnConnected() override
  {
    connected = true;
    printf("Connection established\n");
  }

  void OnDisconnected() override { connected = false; }

  void OnReliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    printf("Packet received '%.*s'\n", static_cast<int>(size), static_cast<const char*>(data));
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    OnReliableReceived(channel, data, size);
  }

  bool connected = false;
};

int main(int argc, const char** argv)
{
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_PACKET_STREAM_PORT", 53473);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr)
  {
    printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr || socket->Bind(SocketConstants::Any(), 0) != SocketError::kNone)
  {
    printf("Cannot create client socket\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(SocketConstants::Loopback(), port);

  auto lastSend = std::chrono::steady_clock::now();
  int counter = 0;

  while (true)
  {
    connection.Tick();
    if (handler.connected)
    {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() > 100)
      {
        lastSend = now;
        std::string packet = "packet#" + std::to_string(counter++);
        connection.SendReliable(1, packet.c_str(), packet.size() + 1);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

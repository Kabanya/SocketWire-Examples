#include <chrono>
#include <cstdio>
#include <print>
#include <string>
#include <thread>

#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

class ClientHandler final : public IReliableConnectionHandler {
 public:
  void OnConnected() override {
    connected = true;
    std::println("Connection established");
  }

  void OnDisconnected() override { connected = false; }

  void OnReliableReceived([[maybe_unused]] std::uint8_t channel,
                          const void* data, std::size_t size) override {
    std::println("Packet received '{:.{}}'", static_cast<const char*>(data),
                 static_cast<int>(size));
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    OnReliableReceived(channel, data, size);
  }

  bool connected = false;
};

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
      socket->Bind(SocketConstants::Any(), 0) != SocketError::kNone) {
    std::println("Cannot create client socket");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(SocketConstants::Loopback(), port);

  auto last_send = std::chrono::steady_clock::now();
  int counter = 0;

  while (true) {
    connection.Update();
    if (handler.connected) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send)
            .count() > 100) {
        last_send = now;
        std::string const packet = "packet#" + std::to_string(counter++);
        connection.SendReliable(0, packet.c_str(), packet.size() + 1);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

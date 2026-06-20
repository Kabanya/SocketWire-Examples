#include <chrono>
#include <cstdio>
#include <print>
#include <thread>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

class ClientHandler final : public IReliableConnectionHandler {
 public:
  void OnConnected() override {
    connected = true;
    std::println("connected");
  }

  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t channel, const void* data,
                          std::size_t size) override {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto type_value = stream.TryRead<std::uint8_t>();
    if (!type_value) return;

    const auto type = static_cast<channels_demo::MessageType>(*type_value);
    if (type == channels_demo::MessageType::kCommandAck) {
      const auto command_id = stream.TryRead<std::uint32_t>();
      if (command_id) {
        std::println("channel {} reliable ack for command #{}", channel,
                     *command_id);
      }
      return;
    }

    if (type == channels_demo::MessageType::kSnapshot) {
      const auto tick = stream.TryRead<std::uint32_t>();
      const auto x = stream.TryRead<float>();
      const auto y = stream.TryRead<float>();
      if (tick && x && y) {
        ++snapshots;
        std::println("channel {} unreliable snapshot tick={} x={:.2f} y={:.2f}",
                     channel, *tick, *x, *y);
      }
    }
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    OnReliableReceived(channel, data, size);
  }

  bool connected = false;
  int snapshots = 0;
};

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_CHANNELS_DEMO_PORT", channels_demo::kKPort);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Cannot init SocketWire");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(socket_constants::Any(), 0) != SocketError::kNone) {
    std::println("Cannot create client socket");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socket_constants::Loopback(), port);

  const auto started = std::chrono::steady_clock::now();
  auto last_move = started;
  bool command_sent = false;
  std::uint32_t tick = 0;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
    connection.Update();

    if (handler.connected && !command_sent) {
      auto command = channels_demo::MakeCommand(1, "open-door");
      connection.SendReliable(0, command);
      command_sent = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (handler.connected && now - last_move > std::chrono::milliseconds(80)) {
      last_move = now;
      const float x = static_cast<float>(tick) * 0.25f;
      const float y = 10.0f + static_cast<float>(tick % 4);
      auto movement = channels_demo::MakeMovement(tick++, x, y);
      connection.SendUnreliable(1, movement);
    }

    if (handler.snapshots >= 5) break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::println("channels-demo client finished with {} snapshots",
               handler.snapshots);
  return handler.snapshots > 0 ? 0 : 1;
}

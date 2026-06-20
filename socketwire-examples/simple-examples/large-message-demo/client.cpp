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

  void OnReliableReceived(std::uint8_t, const void* data,
                          std::size_t size) override {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto type_value = stream.TryRead<std::uint8_t>();
    if (!type_value ||
        static_cast<large_message_demo::MessageType>(*type_value) !=
          large_message_demo::MessageType::kBlobAck) {
      return;
    }

    const auto received_size = stream.TryRead<std::uint32_t>();
    const auto expected_checksum = stream.TryRead<std::uint32_t>();
    const auto actual_checksum = stream.TryRead<std::uint32_t>();
    if (!received_size || !expected_checksum || !actual_checksum) return;

    ackOk = *received_size == large_message_demo::kKPayloadSize &&
            *expected_checksum == *actual_checksum;
    std::println("server ack: size={} checksum={:08x} status={}",
                 *received_size, *actual_checksum, ackOk ? "ok" : "bad");
  }

  void OnUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  bool ackOk = false;
};

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_LARGE_MESSAGE_DEMO_PORT",
    large_message_demo::kKPort);

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
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socket_constants::Loopback(), port);

  const auto started = std::chrono::steady_clock::now();
  bool sent = false;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
    connection.Update();
    if (handler.connected && !sent) {
      const auto payload = large_message_demo::MakePayload();
      auto blob = large_message_demo::MakeBlob(payload);
      sent = connection.SendReliable(0, blob);
      std::println("sent {}-byte payload through {}-byte packets: {}",
                   payload.size(), cfg.maxPacketSize, sent ? "yes" : "no");
    }

    if (handler.ackOk) break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return handler.ackOk ? 0 : 1;
}

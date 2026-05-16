#include <chrono>
#include <cstdio>
#include <print>
#include <thread>
#include <vector>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

static void HandleBlob(socketwire_examples::ServerConnectionHub::Client& client,
                       const void* data, std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto type_value = stream.TryRead<std::uint8_t>();
  const auto declared_size = stream.TryRead<std::uint32_t>();
  const auto expected_checksum = stream.TryRead<std::uint32_t>();
  if (!type_value || !declared_size || !expected_checksum ||
      static_cast<large_message_demo::MessageType>(*type_value) !=
        large_message_demo::MessageType::kBlob) {
    return;
  }

  std::vector<std::uint8_t> payload(stream.GetRemainingBytes());
  try {
    stream.ReadBytes(payload.data(), payload.size());
  } catch (...) {
    return;
  }

  const auto actual_checksum = large_message_demo::Checksum(payload);
  const bool ok =
    payload.size() == *declared_size && actual_checksum == *expected_checksum;
  std::println(
    "large blob received: declared={} actual={} checksum={:08x} "
    "expected={:08x} status={}",
    *declared_size, payload.size(), actual_checksum, *expected_checksum,
    ok ? "ok" : "bad");

  auto ack =
    large_message_demo::MakeAck(static_cast<std::uint32_t>(payload.size()),
                                *expected_checksum, actual_checksum);
  client.connection->SendReliable(0, ack);
}

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
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::println("Cannot bind large-message-demo server");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  cfg.fragmentTimeoutMs = 3000;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.SetConnectedCallback([](auto& client) {
    std::println("client connected from port {:d}", client.port);
  });
  hub.SetPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { HandleBlob(client, data, size); });

  std::println("large-message-demo server listening on port {}",
               static_cast<unsigned>(port));
  while (true) {
    hub.Poll();
    hub.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

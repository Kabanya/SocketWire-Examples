#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

static void handle_blob(
  socketwire_examples::ServerConnectionHub::Client& client, const void* data,
  std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.TryRead<std::uint8_t>();
  const auto declaredSize = stream.TryRead<std::uint32_t>();
  const auto expectedChecksum = stream.TryRead<std::uint32_t>();
  if (!typeValue || !declaredSize || !expectedChecksum ||
      static_cast<large_message_demo::MessageType>(*typeValue) !=
        large_message_demo::MessageType::Blob) {
    return;
  }

  std::vector<std::uint8_t> payload(stream.GetRemainingBytes());
  try {
    stream.ReadBytes(payload.data(), payload.size());
  } catch (...) {
    return;
  }

  const auto actualChecksum = large_message_demo::checksum(payload);
  const bool ok =
    payload.size() == *declaredSize && actualChecksum == *expectedChecksum;
  std::printf(
    "large blob received: declared=%u actual=%zu checksum=%08x expected=%08x "
    "status=%s\n",
    *declaredSize, payload.size(), actualChecksum, *expectedChecksum,
    ok ? "ok" : "bad");

  auto ack =
    large_message_demo::make_ack(static_cast<std::uint32_t>(payload.size()),
                                 *expectedChecksum, actualChecksum);
  client.connection->SendReliable(0, ack);
}

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_LARGE_MESSAGE_DEMO_PORT",
    large_message_demo::K_PORT);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::printf("Cannot bind large-message-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  cfg.fragmentTimeoutMs = 3000;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client) {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { handle_blob(client, data, size); });

  std::printf("large-message-demo server listening on port %u\n",
              static_cast<unsigned>(port));
  while (true) {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

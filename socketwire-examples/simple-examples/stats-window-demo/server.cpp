#include <chrono>
#include <cstdio>
#include <thread>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

static void handle_sample(
  socketwire_examples::ServerConnectionHub::Client& client, const void* data,
  std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.TryRead<std::uint8_t>();
  const auto id = stream.TryRead<std::uint32_t>();
  if (!typeValue || !id ||
      static_cast<stats_window_demo::MessageType>(*typeValue) !=
        stats_window_demo::MessageType::Sample) {
    return;
  }

  std::printf("sample #%u received; replying with reliable ack\n", *id);
  auto ack = stats_window_demo::make_sample_ack(*id);
  client.connection->SendReliable(0, ack);
}

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_STATS_WINDOW_DEMO_PORT",
    stats_window_demo::K_PORT);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::printf("Cannot bind stats-window-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.sendWindowSize = 0;
  cfg.retryTimeoutMs = 80;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client) {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { handle_sample(client, data, size); });

  std::printf("stats-window-demo server listening on port %u\n",
              static_cast<unsigned>(port));
  while (true) {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

#include <chrono>
#include <cstdio>
#include <print>
#include <thread>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

static void HandleSample(
  socketwire_examples::ServerConnectionHub::Client& client, const void* data,
  std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto type_value = stream.TryRead<std::uint8_t>();
  const auto id = stream.TryRead<std::uint32_t>();
  if (!type_value || !id ||
      static_cast<stats_window_demo::MessageType>(*type_value) !=
        stats_window_demo::MessageType::kSample) {
    return;
  }

  std::println("sample #{} received; replying with reliable ack", *id);
  auto ack = stats_window_demo::MakeSampleAck(*id);
  client.connection->SendReliable(0, ack);
}

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_STATS_WINDOW_DEMO_PORT",
    stats_window_demo::kKPort);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Cannot init SocketWire");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::println("Cannot bind stats-window-demo server");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.sendWindowSize = 0;
  cfg.retryTimeoutMs = 80;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.SetConnectedCallback([](auto& client) {
    std::println("client connected from port {:d}", client.port);
  });
  hub.SetPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { HandleSample(client, data, size); });

  std::println("stats-window-demo server listening on port {}",
               static_cast<unsigned>(port));
  while (true) {
    hub.Poll();
    hub.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

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

static void handle_packet(
  socketwire_examples::ServerConnectionHub::Client& client, const void* data,
  std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.TryRead<std::uint8_t>();
  if (!typeValue) return;

  const auto type = static_cast<channels_demo::MessageType>(*typeValue);
  if (type == channels_demo::MessageType::Command) {
    const auto commandId = stream.TryRead<std::uint32_t>();
    const auto text = stream.TryReadString();
    if (!commandId || !text) return;

    std::printf("channel 0 reliable command #%u: %s\n", *commandId,
                text->c_str());
    auto ack = channels_demo::make_command_ack(*commandId);
    client.connection->SendReliable(0, ack);
    return;
  }

  if (type == channels_demo::MessageType::Movement) {
    const auto tick = stream.TryRead<std::uint32_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    if (!tick || !x || !y) return;

    std::printf("channel 1 unreliable movement tick=%u x=%.2f y=%.2f\n", *tick,
                *x, *y);
    auto snapshot = channels_demo::make_snapshot(*tick, *x, *y);
    client.connection->SendUnsequenced(1, snapshot);
  }
}

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_CHANNELS_DEMO_PORT", channels_demo::K_PORT);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::printf("Cannot bind channels-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client) {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t channel, const void* data,
                           std::size_t size, bool reliable) {
    std::printf("packet callback: channel=%u reliable-callback=%s\n", channel,
                reliable ? "yes" : "no");
    handle_packet(client, data, size);
  });

  std::printf("channels-demo server listening on port %u\n",
              static_cast<unsigned>(port));
  while (true) {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

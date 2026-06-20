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

static void HandlePacket(
  socketwire_examples::ServerConnectionHub::Client& client, const void* data,
  std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto type_value = stream.TryRead<std::uint8_t>();
  if (!type_value) return;

  const auto type = static_cast<channels_demo::MessageType>(*type_value);
  if (type == channels_demo::MessageType::kCommand) {
    const auto command_id = stream.TryRead<std::uint32_t>();
    const auto text = stream.TryReadString();
    if (!command_id || !text) return;

    std::println("channel 0 reliable command #{}: {}", *command_id, *text);
    auto ack = channels_demo::MakeCommandAck(*command_id);
    client.connection->SendReliable(0, ack);
    return;
  }

  if (type == channels_demo::MessageType::kMovement) {
    const auto tick = stream.TryRead<std::uint32_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    if (!tick || !x || !y) return;

    std::println("channel 1 unreliable movement tick={} x={:.2f} y={:.2f}",
                 *tick, *x, *y);
    auto snapshot = channels_demo::MakeSnapshot(*tick, *x, *y);
    client.connection->SendUnreliable(1, snapshot);
  }
}

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
      socket->Bind(socket_constants::Any(), port) != SocketError::kNone) {
    std::println("Cannot bind channels-demo server");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.SetConnectedCallback([](auto& client) {
    std::println("client connected from port {:d}", client.port);
  });
  hub.SetPacketCallback([](auto& client, std::uint8_t channel, const void* data,
                           std::size_t size, bool reliable) {
    std::println("packet callback: channel={} reliable-callback={}", channel,
                 reliable ? "yes" : "no");
    HandlePacket(client, data, size);
  });

  std::println("channels-demo server listening on port {}",
               static_cast<unsigned>(port));
  while (true) {
    hub.Poll();
    hub.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

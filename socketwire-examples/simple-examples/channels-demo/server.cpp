#include "protocol.hpp"
#include "server_connection_hub.hpp"

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace socketwire; // NOLINT

static void handle_packet(socketwire_examples::ServerConnectionHub::Client& client,
                          const void* data,
                          std::size_t size)
{
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.try_read<std::uint8_t>();
  if (!typeValue)
    return;

  const auto type = static_cast<channels_demo::MessageType>(*typeValue);
  if (type == channels_demo::MessageType::Command)
  {
    const auto commandId = stream.try_read<std::uint32_t>();
    const auto text = stream.try_readString();
    if (!commandId || !text)
      return;

    std::printf("channel 0 reliable command #%u: %s\n", *commandId, text->c_str());
    auto ack = channels_demo::make_command_ack(*commandId);
    client.connection->sendReliable(0, ack);
    return;
  }

  if (type == channels_demo::MessageType::Movement)
  {
    const auto tick = stream.try_read<std::uint32_t>();
    const auto x = stream.try_read<float>();
    const auto y = stream.try_read<float>();
    if (!tick || !x || !y)
      return;

    std::printf("channel 1 unreliable movement tick=%u x=%.2f y=%.2f\n", *tick, *x, *y);
    auto snapshot = channels_demo::make_snapshot(*tick, *x, *y);
    client.connection->sendUnsequenced(1, snapshot);
  }
}

int main()
{
  initialize_sockets();
  auto* factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->createUDPSocket(SocketConfig{});
  if (socket == nullptr || socket->bind(SocketConstants::any(), channels_demo::K_PORT) != SocketError::None)
  {
    std::printf("Cannot bind channels-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client)
  {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t channel, const void* data, std::size_t size, bool reliable)
  {
    std::printf("packet callback: channel=%u reliable-callback=%s\n", channel, reliable ? "yes" : "no");
    handle_packet(client, data, size);
  });

  std::printf("channels-demo server listening on port %u\n", channels_demo::K_PORT);
  while (true)
  {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

#include "protocol.hpp"
#include "server_connection_hub.hpp"

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace socketwire; // NOLINT

static void handle_sample(socketwire_examples::ServerConnectionHub::Client& client,
                          const void* data,
                          std::size_t size)
{
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.try_read<std::uint8_t>();
  const auto id = stream.try_read<std::uint32_t>();
  if (!typeValue || !id ||
      static_cast<stats_window_demo::MessageType>(*typeValue) != stats_window_demo::MessageType::Sample)
  {
    return;
  }

  std::printf("sample #%u received; replying with reliable ack\n", *id);
  auto ack = stats_window_demo::make_sample_ack(*id);
  client.connection->sendReliable(0, ack);
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
  if (socket == nullptr || socket->bind(SocketConstants::any(), stats_window_demo::kPort) != SocketError::None)
  {
    std::printf("Cannot bind stats-window-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.sendWindowSize = 0;
  cfg.retryTimeoutMs = 80;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client)
  {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    handle_sample(client, data, size);
  });

  std::printf("stats-window-demo server listening on port %u\n", stats_window_demo::kPort);
  while (true)
  {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

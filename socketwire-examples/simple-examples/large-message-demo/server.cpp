#include "protocol.hpp"
#include "server_connection_hub.hpp"

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace socketwire; // NOLINT

static void handle_blob(socketwire_examples::ServerConnectionHub::Client& client,
                        const void* data,
                        std::size_t size)
{
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  const auto typeValue = stream.try_read<std::uint8_t>();
  const auto declaredSize = stream.try_read<std::uint32_t>();
  const auto expectedChecksum = stream.try_read<std::uint32_t>();
  if (!typeValue || !declaredSize || !expectedChecksum ||
      static_cast<large_message_demo::MessageType>(*typeValue) != large_message_demo::MessageType::Blob)
  {
    return;
  }

  std::vector<std::uint8_t> payload(stream.getRemainingBytes());
  try
  {
    stream.readBytes(payload.data(), payload.size());
  }
  catch (...)
  {
    return;
  }

  const auto actualChecksum = large_message_demo::checksum(payload);
  const bool ok = payload.size() == *declaredSize && actualChecksum == *expectedChecksum;
  std::printf("large blob received: declared=%u actual=%zu checksum=%08x expected=%08x status=%s\n",
              *declaredSize,
              payload.size(),
              actualChecksum,
              *expectedChecksum,
              ok ? "ok" : "bad");

  auto ack = large_message_demo::make_ack(static_cast<std::uint32_t>(payload.size()),
                                          *expectedChecksum,
                                          actualChecksum);
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
  if (socket == nullptr || socket->bind(SocketConstants::any(), large_message_demo::K_PORT) != SocketError::None)
  {
    std::printf("Cannot bind large-message-demo server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  cfg.fragmentTimeoutMs = 3000;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setConnectedCallback([](auto& client)
  {
    std::printf("client connected from port %u\n", client.port);
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    handle_blob(client, data, size);
  });

  std::printf("large-message-demo server listening on port %u\n", large_message_demo::K_PORT);
  while (true)
  {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

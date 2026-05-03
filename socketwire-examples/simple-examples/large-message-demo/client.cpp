#include "protocol.hpp"

#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace socketwire; // NOLINT

class ClientHandler final : public IReliableConnectionHandler
{
public:
  void onConnected() override
  {
    connected = true;
    std::printf("connected\n");
  }

  void onDisconnected() override { connected = false; }

  void onReliableReceived(std::uint8_t, const void* data, std::size_t size) override
  {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto typeValue = stream.try_read<std::uint8_t>();
    if (!typeValue || static_cast<large_message_demo::MessageType>(*typeValue) != large_message_demo::MessageType::BlobAck)
      return;

    const auto receivedSize = stream.try_read<std::uint32_t>();
    const auto expectedChecksum = stream.try_read<std::uint32_t>();
    const auto actualChecksum = stream.try_read<std::uint32_t>();
    if (!receivedSize || !expectedChecksum || !actualChecksum)
      return;

    ackOk = *receivedSize == large_message_demo::kPayloadSize && *expectedChecksum == *actualChecksum;
    std::printf("server ack: size=%u checksum=%08x status=%s\n",
                *receivedSize,
                *actualChecksum,
                ackOk ? "ok" : "bad");
  }

  void onUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  bool ackOk = false;
};

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
  if (socket == nullptr || socket->bind(SocketConstants::any(), 0) != SocketError::None)
  {
    std::printf("Cannot create client socket\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(SocketConstants::loopback(), large_message_demo::kPort);

  const auto started = std::chrono::steady_clock::now();
  bool sent = false;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
  {
    connection.tick();
    if (handler.connected && !sent)
    {
      const auto payload = large_message_demo::make_payload();
      auto blob = large_message_demo::make_blob(payload);
      sent = connection.sendReliable(0, blob);
      std::printf("sent %zu-byte payload through %u-byte packets: %s\n",
                  payload.size(),
                  cfg.maxPacketSize,
                  sent ? "yes" : "no");
    }

    if (handler.ackOk)
      break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return handler.ackOk ? 0 : 1;
}

#include "protocol.hpp"

#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace socketwire; // NOLINT

class ClientHandler final : public IReliableConnectionHandler
{
public:
  void OnConnected() override
  {
    connected = true;
    std::printf("connected\n");
  }

  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t, const void* data, std::size_t size) override
  {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto typeValue = stream.TryRead<std::uint8_t>();
    if (!typeValue || static_cast<large_message_demo::MessageType>(*typeValue) != large_message_demo::MessageType::BlobAck)
      return;

    const auto receivedSize = stream.TryRead<std::uint32_t>();
    const auto expectedChecksum = stream.TryRead<std::uint32_t>();
    const auto actualChecksum = stream.TryRead<std::uint32_t>();
    if (!receivedSize || !expectedChecksum || !actualChecksum)
      return;

    ackOk = *receivedSize == large_message_demo::K_PAYLOAD_SIZE && *expectedChecksum == *actualChecksum;
    std::printf("server ack: size=%u checksum=%08x status=%s\n",
                *receivedSize,
                *actualChecksum,
                ackOk ? "ok" : "bad");
  }

  void OnUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  bool ackOk = false;
};

int main(int argc, const char** argv)
{
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_LARGE_MESSAGE_DEMO_PORT", large_message_demo::K_PORT);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr || socket->Bind(SocketConstants::Any(), 0) != SocketError::kNone)
  {
    std::printf("Cannot create client socket\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 1;
  cfg.maxPacketSize = 256;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(SocketConstants::Loopback(), port);

  const auto started = std::chrono::steady_clock::now();
  bool sent = false;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
  {
    connection.Tick();
    if (handler.connected && !sent)
    {
      const auto payload = large_message_demo::make_payload();
      auto blob = large_message_demo::make_blob(payload);
      sent = connection.SendReliable(0, blob);
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

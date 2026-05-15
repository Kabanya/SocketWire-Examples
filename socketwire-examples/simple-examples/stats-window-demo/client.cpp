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
    const auto id = stream.TryRead<std::uint32_t>();
    if (!typeValue || !id ||
        static_cast<stats_window_demo::MessageType>(*typeValue) != stats_window_demo::MessageType::SampleAck)
    {
      return;
    }

    ++acks;
    std::printf("user ack for sample #%u (%u/%u)\n", *id, acks, stats_window_demo::K_PACKET_COUNT);
  }

  void OnUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  std::uint32_t acks = 0;
};

int main(int argc, const char** argv)
{
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_STATS_WINDOW_DEMO_PORT", stats_window_demo::K_PORT);

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
  cfg.sendWindowSize = 4;
  cfg.retryTimeoutMs = 80;
  cfg.pingIntervalMs = 250;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(SocketConstants::Loopback(), port);

  const auto started = std::chrono::steady_clock::now();
  auto lastStats = started;
  std::uint32_t nextSample = 0;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(6))
  {
    connection.Tick();

    while (handler.connected && nextSample < stats_window_demo::K_PACKET_COUNT)
    {
      auto sample = stats_window_demo::make_sample(nextSample);
      if (!connection.SendReliable(0, sample))
        break;
      ++nextSample;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - lastStats > std::chrono::milliseconds(250))
    {
      lastStats = now;
      std::printf("stats: sent=%u received=%u lost=%u inflight=%u window=%u rtt=%.1fms queued=%u\n",
                  connection.GetSentPackets(),
                  connection.GetReceivedPackets(),
                  connection.GetLostPackets(),
                  connection.GetInflightCount(),
                  connection.GetSendWindow(),
                  connection.GetRtt(),
                  nextSample);
    }

    if (handler.acks >= stats_window_demo::K_PACKET_COUNT)
      break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf("final stats: sent=%u received=%u lost=%u inflight=%u window=%u rtt=%.1fms\n",
              connection.GetSentPackets(),
              connection.GetReceivedPackets(),
              connection.GetLostPackets(),
              connection.GetInflightCount(),
              connection.GetSendWindow(),
              connection.GetRtt());
  std::printf("stats-window-demo finished with %u user-level acks\n", handler.acks);
  return handler.acks == stats_window_demo::K_PACKET_COUNT ? 0 : 1;
}

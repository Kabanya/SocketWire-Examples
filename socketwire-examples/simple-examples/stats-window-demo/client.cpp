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
    const auto id = stream.try_read<std::uint32_t>();
    if (!typeValue || !id ||
        static_cast<stats_window_demo::MessageType>(*typeValue) != stats_window_demo::MessageType::SampleAck)
    {
      return;
    }

    ++acks;
    std::printf("user ack for sample #%u (%u/%u)\n", *id, acks, stats_window_demo::K_PACKET_COUNT);
  }

  void onUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  std::uint32_t acks = 0;
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
  cfg.sendWindowSize = 4;
  cfg.retryTimeoutMs = 80;
  cfg.pingIntervalMs = 250;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(SocketConstants::loopback(), stats_window_demo::K_PORT);

  const auto started = std::chrono::steady_clock::now();
  auto lastStats = started;
  std::uint32_t nextSample = 0;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(6))
  {
    connection.tick();

    while (handler.connected && nextSample < stats_window_demo::K_PACKET_COUNT)
    {
      auto sample = stats_window_demo::make_sample(nextSample);
      if (!connection.sendReliable(0, sample))
        break;
      ++nextSample;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - lastStats > std::chrono::milliseconds(250))
    {
      lastStats = now;
      std::printf("stats: sent=%u received=%u lost=%u inflight=%u window=%u rtt=%.1fms queued=%u\n",
                  connection.getSentPackets(),
                  connection.getReceivedPackets(),
                  connection.getLostPackets(),
                  connection.getInflightCount(),
                  connection.getSendWindow(),
                  connection.getRTT(),
                  nextSample);
    }

    if (handler.acks >= stats_window_demo::K_PACKET_COUNT)
      break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf("final stats: sent=%u received=%u lost=%u inflight=%u window=%u rtt=%.1fms\n",
              connection.getSentPackets(),
              connection.getReceivedPackets(),
              connection.getLostPackets(),
              connection.getInflightCount(),
              connection.getSendWindow(),
              connection.getRTT());
  std::printf("stats-window-demo finished with %u user-level acks\n", handler.acks);
  return handler.acks == stats_window_demo::K_PACKET_COUNT ? 0 : 1;
}

#include <chrono>
#include <cstdio>
#include <print>
#include <thread>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

class ClientHandler final : public IReliableConnectionHandler {
 public:
  void OnConnected() override {
    connected = true;
    std::println("connected");
  }

  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t, const void* data,
                          std::size_t size) override {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto type_value = stream.TryRead<std::uint8_t>();
    const auto id = stream.TryRead<std::uint32_t>();
    if (!type_value || !id ||
        static_cast<stats_window_demo::MessageType>(*type_value) !=
          stats_window_demo::MessageType::kSampleAck) {
      return;
    }

    ++acks;
    std::println("user ack for sample #{} ({}/{})", *id, acks,
                 stats_window_demo::kKPacketCount);
  }

  void OnUnreliableReceived(std::uint8_t, const void*, std::size_t) override {}

  bool connected = false;
  std::uint32_t acks = 0;
};

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
      socket->Bind(socket_constants::Any(), 0) != SocketError::kNone) {
    std::println("Cannot create client socket");
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
  connection.Connect(socket_constants::Loopback(), port);

  const auto started = std::chrono::steady_clock::now();
  auto last_stats = started;
  std::uint32_t next_sample = 0;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(6)) {
    connection.Update();

    while (handler.connected &&
           next_sample < stats_window_demo::kKPacketCount) {
      auto sample = stats_window_demo::MakeSample(next_sample);
      if (!connection.SendReliable(0, sample)) break;
      ++next_sample;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_stats > std::chrono::milliseconds(250)) {
      last_stats = now;
      std::println(
        "stats: sent={} received={} lost={} inflight={} window={} rtt={:.1f}ms "
        "queued={}",
        connection.GetSentPackets(), connection.GetReceivedPackets(),
        connection.GetLostPackets(), connection.GetInflightCount(),
        connection.GetSendWindow(), connection.GetRtt(), next_sample);
    }

    if (handler.acks >= stats_window_demo::kKPacketCount) break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::println(
    "final stats: sent={} received={} lost={} inflight={} window={} "
    "rtt={:.1f}ms",
    connection.GetSentPackets(), connection.GetReceivedPackets(),
    connection.GetLostPackets(), connection.GetInflightCount(),
    connection.GetSendWindow(), connection.GetRtt());
  std::println("stats-window-demo finished with {} user-level acks",
               handler.acks);
  return handler.acks == stats_window_demo::kKPacketCount ? 0 : 1;
}

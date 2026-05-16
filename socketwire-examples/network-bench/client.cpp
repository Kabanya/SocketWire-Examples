#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

#include "netbench_common.hpp"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

namespace {

class Handler final : public socketwire::IReliableConnectionHandler {
 public:
  explicit Handler(netbench::AppStats& stats) : stats_(stats) {}

  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }
  void OnTimeout() override { connected = false; }

  void OnReliableReceived(std::uint8_t, const void* data,
                          std::size_t size) override {
    Receive(data, size);
  }

  void OnUnreliableReceived(std::uint8_t, const void* data,
                            std::size_t size) override {
    Receive(data, size);
  }

  bool connected = false;

 private:
  void Receive(const void* data, std::size_t size) {
    netbench::PacketHeader header;
    if (!netbench::parseHeader(data, size, header) ||
        !netbench::measured(header)) {
      return;
    }
    stats_.noteRx(size);
    stats_.noteEcho(header, netbench::nowUs());
  }

  netbench::AppStats& stats_;
};

bool SendPayload(socketwire::ReliableConnection& connection,
                 netbench::ScheduledStream& stream, netbench::PacketKind kind,
                 bool measured, std::uint32_t client_id,
                 std::uint32_t& next_sequence, std::uint32_t seed,
                 netbench::AppStats& stats) {
  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::uint32_t sequence = next_sequence++;
  const auto flags = static_cast<std::uint8_t>(
    stream.flags | (measured ? netbench::FlagMeasured : 0));
  const auto size = netbench::makePayload(payload.data(), stream.bytes, kind,
                                          flags, stream.channel, client_id,
                                          sequence, netbench::nowUs(), seed);

  bool sent = false;
  if ((stream.flags & netbench::FlagReliable) != 0) {
    sent = connection.SendReliable(stream.channel, payload.data(), size);
  } else if ((stream.flags & netbench::FlagUnsequenced) != 0) {
    sent = connection.SendUnsequenced(stream.channel, payload.data(), size);
  } else {
    sent = connection.SendUnreliable(stream.channel, payload.data(), size);
  }

  if (sent && measured) stats.noteTx(size, true);
  return sent;
}

void SendDue(socketwire::ReliableConnection& connection,
             netbench::ScheduledStream& stream, netbench::PacketKind kind,
             bool measured, std::uint32_t client_id,
             std::uint32_t& next_sequence, std::uint32_t seed,
             netbench::AppStats& stats) {
  const auto now = netbench::nowUs();
  int burst = 0;
  while (stream.due(now) && burst < 16) {
    SendPayload(connection, stream, kind, measured, client_id, next_sequence,
                seed, stats);
    stream.advance();
    burst += 1;
  }
}

netbench::TransportStats TransportStats(
  const socketwire::ReliableConnection& connection) {
  netbench::TransportStats stats;
  stats.rttMs = connection.GetRtt();
  stats.lostPackets = connection.GetLostPackets();
  stats.inflightPackets = connection.GetInflightCount();
  stats.sendWindow = connection.GetSendWindow();
  return stats;
}

}  // namespace

int main(int argc, const char** argv) {
  auto options = netbench::parseOptions(argc, argv, 53490);
  options.enabled = true;

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  cfg.maxPacketSize = 1400;
  cfg.pingIntervalMs = 600000;

  netbench::AppStats stats;
  const auto profile = netbench::profileByName(options.profile);
  const auto expected_packets = static_cast<std::size_t>(
    (profile.reliablePps + profile.unreliablePps + profile.pingPps + 128U) *
    std::max(options.durationMs, 1) / 1000);
  stats.reserve(expected_packets);

  netbench::MetricsWriter metrics(options, "socketwire", "client");
  Handler handler(stats);
  socketwire::ReliableConnection connection(socket.get(), cfg);
  connection.SetHandler(&handler);
  const auto server_address = socketwire_examples::ResolveAddress(options.host);
  connection.Connect(server_address, options.port);
  auto next_connect_attempt =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(250);

  const std::uint32_t client_id = options.seed;
  std::uint32_t next_sequence = 0;
  auto reliable = netbench::ScheduledStream{
    profile.reliablePps, profile.reliableBytes, netbench::FlagReliable, 0, 0,
    netbench::nowUs(),
  };
  auto unreliable = netbench::ScheduledStream{
    profile.unreliablePps,
    profile.unreliableBytes,
    static_cast<std::uint8_t>(profile.unsequenced ? netbench::FlagUnsequenced
                                                  : 0),
    1,
    0,
    netbench::nowUs(),
  };
  auto ping = netbench::ScheduledStream{
    profile.pingPps,   netbench::kHeaderSize, netbench::FlagReliable, 0, 0,
    netbench::nowUs(),
  };

  bool reset_at_measurement = false;
  while (!metrics.done()) {
    const auto loop_start = std::chrono::steady_clock::now();
    if (!handler.connected && loop_start >= next_connect_attempt) {
      connection.Connect(server_address, options.port);
      next_connect_attempt = loop_start + std::chrono::milliseconds(250);
    }
    connection.Tick();

    const bool measuring = metrics.measuring();
    if (measuring && !reset_at_measurement) {
      const auto now = netbench::nowUs();
      reliable.reset(now);
      unreliable.reset(now);
      ping.reset(now);
      reset_at_measurement = true;
    }

    if (handler.connected && measuring) {
      if (profile.flood) {
        for (int i = 0; i < 32; ++i) {
          SendPayload(connection, unreliable, netbench::PacketKind::Data,
                      measuring, client_id, next_sequence, options.seed, stats);
        }
      } else {
        SendDue(connection, reliable, netbench::PacketKind::Data, measuring,
                client_id, next_sequence, options.seed, stats);
        SendDue(connection, unreliable, netbench::PacketKind::Data, measuring,
                client_id, next_sequence, options.seed, stats);
        SendDue(connection, ping, netbench::PacketKind::Ping, measuring,
                client_id, next_sequence, options.seed, stats);
      }
    }

    metrics.setConnectedClients(handler.connected ? 1 : 0);
    metrics.maybeWriteSample(stats, TransportStats(connection));

    const auto loop_end = std::chrono::steady_clock::now();
    stats.noteUpdateMs(
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            loop_end - loop_start)
                            .count()) /
      1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  connection.Disconnect();
  metrics.finish(&stats, TransportStats(connection));
  return 0;
}

#include "netbench_common.hpp"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{

class Handler final : public socketwire::IReliableConnectionHandler
{
public:
  explicit Handler(netbench::AppStats& stats)
    : stats_(stats)
  {
  }

  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }
  void OnTimeout() override { connected = false; }

  void OnReliableReceived(std::uint8_t, const void* data, std::size_t size) override
  {
    receive(data, size);
  }

  void OnUnreliableReceived(std::uint8_t, const void* data, std::size_t size) override
  {
    receive(data, size);
  }

  bool connected = false;

private:
  void receive(const void* data, std::size_t size)
  {
    netbench::PacketHeader header;
    if (!netbench::parseHeader(data, size, header) || !netbench::measured(header))
      return;
    stats_.noteRx(size);
    stats_.noteEcho(header, netbench::nowUs());
  }

  netbench::AppStats& stats_;
};

bool sendPayload(socketwire::ReliableConnection& connection,
                 netbench::ScheduledStream& stream,
                 netbench::PacketKind kind,
                 bool measured,
                 std::uint32_t clientId,
                 std::uint32_t& nextSequence,
                 std::uint32_t seed,
                 netbench::AppStats& stats)
{
  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::uint32_t sequence = nextSequence++;
  const auto flags = static_cast<std::uint8_t>(stream.flags | (measured ? netbench::FlagMeasured : 0));
  const auto size = netbench::makePayload(payload.data(),
                                          stream.bytes,
                                          kind,
                                          flags,
                                          stream.channel,
                                          clientId,
                                          sequence,
                                          netbench::nowUs(),
                                          seed);

  bool sent = false;
  if ((stream.flags & netbench::FlagReliable) != 0)
    sent = connection.SendReliable(stream.channel, payload.data(), size);
  else if ((stream.flags & netbench::FlagUnsequenced) != 0)
    sent = connection.SendUnsequenced(stream.channel, payload.data(), size);
  else
    sent = connection.SendUnreliable(stream.channel, payload.data(), size);

  if (sent && measured)
    stats.noteTx(size, true);
  return sent;
}

void sendDue(socketwire::ReliableConnection& connection,
             netbench::ScheduledStream& stream,
             netbench::PacketKind kind,
             bool measured,
             std::uint32_t clientId,
             std::uint32_t& nextSequence,
             std::uint32_t seed,
             netbench::AppStats& stats)
{
  const auto now = netbench::nowUs();
  int burst = 0;
  while (stream.due(now) && burst < 16)
  {
    sendPayload(connection, stream, kind, measured, clientId, nextSequence, seed, stats);
    stream.advance();
    burst += 1;
  }
}

netbench::TransportStats transportStats(const socketwire::ReliableConnection& connection)
{
  netbench::TransportStats stats;
  stats.rttMs = connection.GetRtt();
  stats.lostPackets = connection.GetLostPackets();
  stats.inflightPackets = connection.GetInflightCount();
  stats.sendWindow = connection.GetSendWindow();
  return stats;
}

} // namespace

int main(int argc, const char** argv)
{
  auto options = netbench::parseOptions(argc, argv, 53490);
  options.enabled = true;

  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  cfg.maxPacketSize = 1400;
  cfg.pingIntervalMs = 600000;

  netbench::AppStats stats;
  const auto profile = netbench::profileByName(options.profile);
  const auto expectedPackets = static_cast<std::size_t>(
    (profile.reliablePps + profile.unreliablePps + profile.pingPps + 128U) *
    std::max(options.durationMs, 1) / 1000);
  stats.reserve(expectedPackets);

  netbench::MetricsWriter metrics(options, "socketwire", "client");
  Handler handler(stats);
  socketwire::ReliableConnection connection(socket.get(), cfg);
  connection.SetHandler(&handler);
  const auto serverAddress = socketwire_examples::resolveAddress(options.host);
  connection.Connect(serverAddress, options.port);
  auto nextConnectAttempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);

  const std::uint32_t clientId = options.seed;
  std::uint32_t nextSequence = 0;
  auto reliable = netbench::ScheduledStream{
    profile.reliablePps,
    profile.reliableBytes,
    netbench::FlagReliable,
    0,
    0,
    netbench::nowUs(),
  };
  auto unreliable = netbench::ScheduledStream{
    profile.unreliablePps,
    profile.unreliableBytes,
    static_cast<std::uint8_t>(profile.unsequenced ? netbench::FlagUnsequenced : 0),
    1,
    0,
    netbench::nowUs(),
  };
  auto ping = netbench::ScheduledStream{
    profile.pingPps,
    netbench::kHeaderSize,
    netbench::FlagReliable,
    0,
    0,
    netbench::nowUs(),
  };

  bool resetAtMeasurement = false;
  while (!metrics.done())
  {
    const auto loopStart = std::chrono::steady_clock::now();
    if (!handler.connected && loopStart >= nextConnectAttempt)
    {
      connection.Connect(serverAddress, options.port);
      nextConnectAttempt = loopStart + std::chrono::milliseconds(250);
    }
    connection.Tick();

    const bool measuring = metrics.measuring();
    if (measuring && !resetAtMeasurement)
    {
      const auto now = netbench::nowUs();
      reliable.reset(now);
      unreliable.reset(now);
      ping.reset(now);
      resetAtMeasurement = true;
    }

    if (handler.connected && measuring)
    {
      if (profile.flood)
      {
        for (int i = 0; i < 32; ++i)
          sendPayload(connection,
                      unreliable,
                      netbench::PacketKind::Data,
                      measuring,
                      clientId,
                      nextSequence,
                      options.seed,
                      stats);
      }
      else
      {
        sendDue(connection, reliable, netbench::PacketKind::Data, measuring, clientId, nextSequence, options.seed, stats);
        sendDue(connection,
                unreliable,
                netbench::PacketKind::Data,
                measuring,
                clientId,
                nextSequence,
                options.seed,
                stats);
        sendDue(connection, ping, netbench::PacketKind::Ping, measuring, clientId, nextSequence, options.seed, stats);
      }
    }

    metrics.setConnectedClients(handler.connected ? 1 : 0);
    metrics.maybeWriteSample(stats, transportStats(connection));

    const auto loopEnd = std::chrono::steady_clock::now();
    stats.noteUpdateMs(static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count()) / 1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  connection.Disconnect();
  metrics.finish(&stats, transportStats(connection));
  return 0;
}

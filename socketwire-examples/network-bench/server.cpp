#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "netbench_common.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

namespace {

bool SendEcho(socketwire_examples::ServerConnectionHub::Client& client,
              const netbench::PacketHeader& header, const void* data,
              std::size_t size) {
  if (client.connection == nullptr || !client.connection->IsConnected()) {
    return false;
  }

  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::size_t copy_size = std::min(size, payload.size());
  std::memcpy(payload.data(), data, copy_size);
  payload[4] = static_cast<std::uint8_t>(netbench::PacketKind::Echo);

  if ((header.flags & netbench::FlagReliable) != 0) {
    return client.connection->SendReliable(header.channel, payload.data(),
                                           copy_size);
  }
  if ((header.flags & netbench::FlagUnsequenced) != 0) {
    return client.connection->SendUnsequenced(header.channel, payload.data(),
                                              copy_size);
  }
  return client.connection->SendUnreliable(header.channel, payload.data(),
                                           copy_size);
}

netbench::TransportStats TransportStats(
  const std::vector<socketwire_examples::ServerConnectionHub::Client*>&
    clients) {
  netbench::TransportStats stats;
  std::uint64_t connected = 0;
  for (const auto* client : clients) {
    if (client == nullptr || client->connection == nullptr ||
        !client->connection->IsConnected()) {
      continue;
    }
    stats.rttMs += client->connection->GetRtt();
    stats.lostPackets += client->connection->GetLostPackets();
    stats.inflightPackets += client->connection->GetInflightCount();
    stats.sendWindow += client->connection->GetSendWindow();
    connected += 1;
  }
  if (connected > 0) stats.rttMs /= static_cast<double>(connected);
  return stats;
}

}  // namespace

int main(int argc, const char** argv) {
  auto options = netbench::parseOptions(argc, argv, 53490);
  options.enabled = true;

  auto socket = socketwire_examples::CreateUdpSocket(options.port);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  cfg.maxPacketSize = 1400;
  cfg.pingIntervalMs = 600000;
  cfg.maxHandshakesPerSecond = 0;

  netbench::AppStats stats;
  const auto profile = netbench::profileByName(options.profile);
  const auto expected_packets = static_cast<std::size_t>(
    (profile.reliablePps + profile.unreliablePps + profile.pingPps + 128U) *
    std::max(options.durationMs, 1) / 1000 * std::max(options.clients, 1));
  stats.reserve(expected_packets);

  netbench::MetricsWriter metrics(options, "socketwire", "server");
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      netbench::PacketHeader header;
      if (!netbench::parseHeader(data, size, header)) return;
      if (netbench::measured(header)) stats.noteRx(size);
      if (sendEcho(client, header, data, size) && netbench::measured(header)) {
        stats.noteTx(size, false);
      }
    });

  while (!metrics.done()) {
    const auto loop_start = std::chrono::steady_clock::now();
    hub.Poll();
    hub.Update();

    const auto clients = hub.Clients();
    metrics.setConnectedClients(static_cast<int>(clients.size()));
    metrics.maybeWriteSample(stats, TransportStats(clients));

    const auto loop_end = std::chrono::steady_clock::now();
    stats.noteUpdateMs(
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            loop_end - loop_start)
                            .count()) /
      1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.finish(&stats, TransportStats(hub.Clients()));
  return 0;
}

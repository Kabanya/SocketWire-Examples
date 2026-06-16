#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "netbench_common.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

namespace {

socketwire::ReliableConnectionConfig Config() {
  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 8;
  cfg.maxPacketSize = netbench::kTransportPacketSize;
  cfg.maxRetries = 600;
  cfg.pingIntervalMs = 600000;
  cfg.disconnectTimeoutMs = 60000;
  cfg.deadlinesEnabled = true;
  cfg.maxdeadline_ms = 5000;
  return cfg;
}

bool SendEcho(socketwire_examples::ServerConnectionHub::Client& client,
              const netbench::PacketHeader& header, const void* data,
              std::size_t size) {
  if (client.connection == nullptr || !client.connection->IsConnected()) {
    return false;
  }

  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::size_t copy_size = std::min(size, payload.size());
  std::memcpy(payload.data(), data, copy_size);
  payload[4] = static_cast<std::uint8_t>(netbench::PacketKind::kEcho);

  switch (header.mode) {
    case netbench::DeliveryMode::kReliable:
      return client.connection->SendReliable(header.channel, payload.data(),
                                             copy_size);
    case netbench::DeliveryMode::kUnreliable:
      return client.connection->SendUnreliable(header.channel, payload.data(),
                                               copy_size);
    case netbench::DeliveryMode::kUnsequenced:
      return client.connection->SendUnsequenced(header.channel, payload.data(),
                                                copy_size);
    case netbench::DeliveryMode::kSequenced:
      return client.connection->SendSequenced(header.channel, payload.data(),
                                              copy_size);
    case netbench::DeliveryMode::kDeadlineReliable:
      return client.connection->SendReliableWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineUnreliable:
      return client.connection->SendUnreliableWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineUnsequenced:
      return client.connection->SendUnsequencedWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineSequenced:
      return client.connection->SendSequencedWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
  }
  return false;
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
    stats.deadlineSendDrops += client->connection->GetDeadlineSendDrops();
    stats.deadlineReceiveDrops += client->connection->GetDeadlineReceiveDrops();
    stats.deadlineRetriesPrevented +=
      client->connection->GetDeadlineRetriesPrevented();
    stats.deadlineExpiredFragmentGroups +=
      client->connection->GetDeadlineExpiredFragmentGroups();
    connected += 1;
  }
  if (connected > 0) stats.rttMs /= static_cast<double>(connected);
  return stats;
}

}  // namespace

int main(int argc, const char** argv) {
  auto options = netbench::parseOptions(argc, argv);
  netbench::AppStats stats;
  netbench::MetricsWriter metrics(options, "server");

  auto socket = socketwire_examples::CreateUdpSocket(options.port);
  if (socket == nullptr) {
    metrics.finish(stats,
                   {.clientsRequested = options.clients,
                    .clientsCreated = 0,
                    .connectedClients = 0,
                    .status = "bind_failed"});
    return 1;
  }

  socketwire_examples::ServerConnectionHub hub(socket.get(), Config());
  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      netbench::PacketHeader header;
      if (!netbench::parseHeader(data, size, header)) {
        stats.malformedPackets += 1;
        return;
      }
      if (!netbench::validPayload(header, data, size)) {
        stats.corruptedPackets += 1;
        return;
      }

      const auto bucket = netbench::bucketForMode(header.mode);
      stats.noteEchoed(bucket, size);
      if (SendEcho(client, header, data, size)) {
        stats.noteSent(bucket, size);
      } else {
        stats.sendFailures += 1;
      }
    });

  while (!metrics.done()) {
    const auto loop_start = netbench::Clock::now();
    hub.Poll();
    hub.Update();

    const auto clients = hub.Clients();
    metrics.maybeWriteSample(
      stats, {.clientsRequested = options.clients,
              .clientsCreated = static_cast<int>(clients.size()),
              .connectedClients = static_cast<int>(clients.size()),
              .status = "running",
              .transport = TransportStats(clients)});

    const auto loop_end = netbench::Clock::now();
    stats.noteUpdateMs(
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            loop_end - loop_start)
                            .count()) /
      1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto clients = hub.Clients();
  metrics.finish(stats, {.clientsRequested = options.clients,
                         .clientsCreated = static_cast<int>(clients.size()),
                         .connectedClients = static_cast<int>(clients.size()),
                         .status = "ok",
                         .transport = TransportStats(clients)});
  return 0;
}

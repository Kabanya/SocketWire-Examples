#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "netbench_common.hpp"
#include "server_connection_hub.hpp"
#include "sharded_connection_manager.hpp"
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

bool SendEcho(socketwire::ReliableConnection& connection,
              const netbench::PacketHeader& header, const void* data,
              std::size_t size) {
  if (!connection.IsConnected()) return false;

  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::size_t copy_size = std::min(size, payload.size());
  std::memcpy(payload.data(), data, copy_size);
  payload[4] = static_cast<std::uint8_t>(netbench::PacketKind::kEcho);

  switch (header.mode) {
    case netbench::DeliveryMode::kReliable:
      return connection.SendReliable(header.channel, payload.data(), copy_size);
    case netbench::DeliveryMode::kUnreliable:
      return connection.SendUnreliable(header.channel, payload.data(),
                                       copy_size);
    case netbench::DeliveryMode::kUnsequenced:
      return connection.SendUnsequenced(header.channel, payload.data(),
                                        copy_size);
    case netbench::DeliveryMode::kSequenced:
      return connection.SendSequenced(header.channel, payload.data(),
                                      copy_size);
    case netbench::DeliveryMode::kDeadlineReliable:
      return connection.SendReliableWithDeadline(header.channel, payload.data(),
                                                 copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineUnreliable:
      return connection.SendUnreliableWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineUnsequenced:
      return connection.SendUnsequencedWithDeadline(
        header.channel, payload.data(), copy_size, 1000);
    case netbench::DeliveryMode::kDeadlineSequenced:
      return connection.SendSequencedWithDeadline(
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
    stats.LostPackets += client->connection->GetLostPackets();
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

netbench::TransportStats TransportStats(
  const socketwire::ShardedConnectionStats& sharded) {
  netbench::TransportStats stats;
  stats.rttMs = sharded.rttMs;
  stats.LostPackets = sharded.lostPackets;
  stats.inflightPackets = sharded.inflightPackets;
  stats.sendWindow = sharded.sendWindow;
  stats.deadlineSendDrops = sharded.deadlineSendDrops;
  stats.deadlineReceiveDrops = sharded.deadlineReceiveDrops;
  stats.deadlineRetriesPrevented = sharded.deadlineRetriesPrevented;
  stats.deadlineExpiredFragmentGroups = sharded.deadlineExpiredFragmentGroups;
  return stats;
}

}  // namespace

int main(int argc, const char** argv) {
  auto options = netbench::ParseOptions(argc, argv);
  netbench::AppStats stats;
  netbench::MetricsWriter metrics(options, "server");

  if (options.serverWorkers > 1) {
    std::mutex stats_mutex;
    socketwire::ShardedConnectionManagerConfig server_cfg;
    server_cfg.port = options.port;
    server_cfg.workerCount = static_cast<std::uint32_t>(options.serverWorkers);
    server_cfg.connection.connection = Config();
    const int capacity =
      options.serverMaxClients > 0 ? options.serverMaxClients : options.clients;
    const int per_worker_capacity =
      (capacity + options.serverWorkers - 1) / options.serverWorkers;
    server_cfg.connection.maxClients =
      static_cast<std::uint32_t>(std::max(per_worker_capacity, 1024));
    server_cfg.connection.maxHandshakesPerSecond = 0;

    socketwire::ShardedConnectionManager server(server_cfg);
    server.SetPacketCallback(
      [&](socketwire::ShardedClientHandle,
          socketwire::ConnectionManager::RemoteClient& client, std::uint8_t,
          const void* data, std::size_t size, bool) {
        netbench::PacketHeader header;
        if (!netbench::ParseHeader(data, size, header)) {
          const std::scoped_lock lock(stats_mutex);
          stats.malformedPackets += 1;
          return;
        }
        if (!netbench::ValidPayload(header, data, size)) {
          const std::scoped_lock lock(stats_mutex);
          stats.corruptedPackets += 1;
          return;
        }

        const auto bucket = netbench::BucketForMode(header.mode);
        const bool sent = client.connection != nullptr &&
                          SendEcho(*client.connection, header, data, size);
        const std::scoped_lock lock(stats_mutex);
        stats.NoteEchoed(bucket, size);
        if (sent) {
          stats.NoteSent(bucket, size);
        } else {
          stats.sendFailures += 1;
        }
      });

    if (!server.Start()) {
      metrics.Finish(stats, {.clientsRequested = options.clients,
                             .clientsCreated = 0,
                             .connectedClients = 0,
                             .serverWorkers = options.serverWorkers,
                             .reusePort = true,
                             .status = "bind_failed"});
      return 1;
    }

    while (!metrics.Done()) {
      const auto loop_start = netbench::Clock::now();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      (void)server.DrainEvents();
      const auto sharded = server.SnapshotStats();

      const auto loop_end = netbench::Clock::now();
      const std::scoped_lock lock(stats_mutex);
      stats.NoteUpdateMs(
        static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end -
                                                                loop_start)
            .count()) /
        1000.0);
      metrics.MaybeWriteSample(
        stats,
        {.clientsRequested = options.clients,
         .clientsCreated = static_cast<int>(sharded.totalClients),
         .connectedClients = static_cast<int>(sharded.connectedClients),
         .serverWorkers = options.serverWorkers,
         .reusePort = server.ReusePortEnabled(),
         .workerConnectedMin = static_cast<int>(sharded.workerConnectedMin),
         .workerConnectedMax = static_cast<int>(sharded.workerConnectedMax),
         .workerUpdateMsAvg = sharded.workerUpdateMsAvg,
         .workerUpdateMsMax = sharded.workerUpdateMsMax,
         .status = "running",
         .transport = TransportStats(sharded)});
    }

    const auto sharded = server.SnapshotStats();
    {
      const std::scoped_lock lock(stats_mutex);
      metrics.Finish(
        stats,
        {.clientsRequested = options.clients,
         .clientsCreated = static_cast<int>(sharded.totalClients),
         .connectedClients = static_cast<int>(sharded.connectedClients),
         .serverWorkers = options.serverWorkers,
         .reusePort = server.ReusePortEnabled(),
         .workerConnectedMin = static_cast<int>(sharded.workerConnectedMin),
         .workerConnectedMax = static_cast<int>(sharded.workerConnectedMax),
         .workerUpdateMsAvg = sharded.workerUpdateMsAvg,
         .workerUpdateMsMax = sharded.workerUpdateMsMax,
         .status = "ok",
         .transport = TransportStats(sharded)});
    }
    server.Stop();
    return 0;
  }

  auto socket = socketwire_examples::CreateUdpSocket(options.port);
  if (socket == nullptr) {
    metrics.Finish(stats, {.clientsRequested = options.clients,
                           .clientsCreated = 0,
                           .connectedClients = 0,
                           .status = "bind_failed"});
    return 1;
  }

  socketwire_examples::ServerConnectionHub hub(socket.get(), Config());
  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      netbench::PacketHeader header;
      if (!netbench::ParseHeader(data, size, header)) {
        stats.malformedPackets += 1;
        return;
      }
      if (!netbench::ValidPayload(header, data, size)) {
        stats.corruptedPackets += 1;
        return;
      }

      const auto bucket = netbench::BucketForMode(header.mode);
      stats.NoteEchoed(bucket, size);
      if (client.connection != nullptr &&
          SendEcho(*client.connection, header, data, size)) {
        stats.NoteSent(bucket, size);
      } else {
        stats.sendFailures += 1;
      }
    });

  while (!metrics.Done()) {
    const auto loop_start = netbench::Clock::now();
    hub.Poll();
    hub.Update();

    const auto clients = hub.Clients();
    metrics.MaybeWriteSample(
      stats, {.clientsRequested = options.clients,
              .clientsCreated = static_cast<int>(clients.size()),
              .connectedClients = static_cast<int>(clients.size()),
              .status = "running",
              .transport = TransportStats(clients)});

    const auto loop_end = netbench::Clock::now();
    stats.NoteUpdateMs(
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            loop_end - loop_start)
                            .count()) /
      1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto clients = hub.Clients();
  metrics.Finish(stats, {.clientsRequested = options.clients,
                         .clientsCreated = static_cast<int>(clients.size()),
                         .connectedClients = static_cast<int>(clients.size()),
                         .status = "ok",
                         .transport = TransportStats(clients)});
  return 0;
}

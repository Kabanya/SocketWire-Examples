#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <print>
#include <thread>
#include <vector>

#include "i_socket.hpp"
#include "netbench_common.hpp"
#include "reliable_connection.hpp"
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
        header.kind != netbench::PacketKind::kEcho) {
      stats_.malformedPackets += 1;
      return;
    }
    if (!netbench::validPayload(header, data, size)) {
      stats_.corruptedPackets += 1;
      return;
    }
    stats_.noteEchoed(netbench::bucketForMode(header.mode), size);
  }

  netbench::AppStats& stats_;
};

struct ClientState {
  ClientState(netbench::AppStats& stats, std::uint32_t id)
      : handler(stats), clientId(id) {}

  std::unique_ptr<socketwire::ISocket> socket;
  std::unique_ptr<socketwire::ReliableConnection> connection;
  Handler handler;
  std::uint32_t clientId = 0;
  std::uint32_t nextSequence = 0;
  std::uint8_t nextDeadlineMode = 0;
  std::array<netbench::ScheduledStream, 5> streams{};
  netbench::Clock::time_point nextConnectAttempt{};
};

void ResetStreams(ClientState& client, const netbench::TrafficProfile& profile,
                  std::uint64_t now_us) {
  client.streams = {
    netbench::ScheduledStream{profile.reliablePps, profile.reliableBytes},
    netbench::ScheduledStream{profile.unreliablePps, profile.unreliableBytes},
    netbench::ScheduledStream{profile.unsequencedPps,
                              profile.unsequencedBytes},
    netbench::ScheduledStream{profile.sequencedPps, profile.sequencedBytes},
    netbench::ScheduledStream{profile.deadlinePps, profile.deadlineBytes},
  };
  for (auto& stream : client.streams) stream.reset(now_us);
}

void DrainSocket(ClientState& client) {
  if (client.socket == nullptr || client.connection == nullptr) return;

  std::array<std::uint8_t, 4096> buffer{};
  while (true) {
    socketwire::SocketAddress from;
    std::uint16_t port = 0;
    const auto result =
      client.socket->Receive(buffer.data(), buffer.size(), from, port);
    if (result.Failed() || result.bytes <= 0) break;
    client.connection->ProcessPacket(
      buffer.data(), static_cast<std::size_t>(result.bytes), from, port);
  }
}

bool SendPayload(ClientState& client, netbench::DeliveryMode mode,
                 std::size_t bytes, const netbench::Options& options,
                 netbench::AppStats& stats) {
  if (client.connection == nullptr || !client.handler.connected) return false;

  std::array<std::uint8_t, netbench::kMaxPayloadSize> payload{};
  const std::size_t size = netbench::makePayload(
    payload.data(), bytes, netbench::PacketKind::kData, mode, client.clientId,
    client.nextSequence++, options.seed);

  bool sent = false;
  const std::uint8_t channel = netbench::channelForMode(mode);
  switch (mode) {
    case netbench::DeliveryMode::kReliable:
      sent = client.connection->SendReliable(channel, payload.data(), size);
      break;
    case netbench::DeliveryMode::kUnreliable:
      sent = client.connection->SendUnreliable(channel, payload.data(), size);
      break;
    case netbench::DeliveryMode::kUnsequenced:
      sent = client.connection->SendUnsequenced(channel, payload.data(), size);
      break;
    case netbench::DeliveryMode::kSequenced:
      sent = client.connection->SendSequenced(channel, payload.data(), size);
      break;
    case netbench::DeliveryMode::kDeadlineReliable:
      sent = client.connection->SendReliableWithDeadline(channel, payload.data(),
                                                         size, 1000);
      break;
    case netbench::DeliveryMode::kDeadlineUnreliable:
      sent = client.connection->SendUnreliableWithDeadline(
        channel, payload.data(), size, 1000);
      break;
    case netbench::DeliveryMode::kDeadlineUnsequenced:
      sent = client.connection->SendUnsequencedWithDeadline(
        channel, payload.data(), size, 1000);
      break;
    case netbench::DeliveryMode::kDeadlineSequenced:
      sent = client.connection->SendSequencedWithDeadline(
        channel, payload.data(), size, 1000);
      break;
  }

  if (sent) {
    stats.noteSent(netbench::bucketForMode(mode), size);
  } else {
    stats.sendFailures += 1;
  }
  return sent;
}

netbench::DeliveryMode NextDeadlineMode(ClientState& client) {
  const std::array<netbench::DeliveryMode, 4> modes{
    netbench::DeliveryMode::kDeadlineReliable,
    netbench::DeliveryMode::kDeadlineUnreliable,
    netbench::DeliveryMode::kDeadlineUnsequenced,
    netbench::DeliveryMode::kDeadlineSequenced,
  };
  const auto mode = modes.at(client.nextDeadlineMode % modes.size());
  client.nextDeadlineMode += 1;
  return mode;
}

void SendDue(ClientState& client, netbench::ScheduledStream& stream,
             netbench::DeliveryMode mode, const netbench::Options& options,
             netbench::AppStats& stats) {
  const auto now_us = netbench::nowUs();
  int burst = 0;
  while (stream.due(now_us) && burst < 8) {
    (void)SendPayload(client, mode, stream.bytes, options, stats);
    stream.advance();
    burst += 1;
  }
}

void SendDeadlineDue(ClientState& client, netbench::ScheduledStream& stream,
                     const netbench::Options& options,
                     netbench::AppStats& stats) {
  const auto now_us = netbench::nowUs();
  int burst = 0;
  while (stream.due(now_us) && burst < 8) {
    (void)SendPayload(client, NextDeadlineMode(client), stream.bytes, options,
                      stats);
    stream.advance();
    burst += 1;
  }
}

int ConnectedClients(const std::vector<std::unique_ptr<ClientState>>& clients) {
  int connected = 0;
  for (const auto& client : clients) {
    if (client->handler.connected) connected += 1;
  }
  return connected;
}

netbench::TransportStats TransportStats(
  const std::vector<std::unique_ptr<ClientState>>& clients) {
  netbench::TransportStats stats;
  std::uint64_t connected = 0;
  for (const auto& client : clients) {
    if (client->connection == nullptr || !client->handler.connected) continue;
    stats.rttMs += client->connection->GetRtt();
    stats.lostPackets += client->connection->GetLostPackets();
    stats.inflightPackets += client->connection->GetInflightCount();
    stats.sendWindow += client->connection->GetSendWindow();
    stats.deadlineSendDrops += client->connection->GetDeadlineSendDrops();
    stats.deadlineReceiveDrops +=
      client->connection->GetDeadlineReceiveDrops();
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
  const auto options = netbench::parseOptions(argc, argv);
  const auto profile = netbench::profileByName(options.profile);
  netbench::AppStats stats;

  const auto server_address = socketwire_examples::ResolveAddress(options.host);
  std::vector<std::unique_ptr<ClientState>> clients;
  clients.reserve(static_cast<std::size_t>(options.clients));

  const auto cfg = Config();
  for (int i = 0; i < options.clients; ++i) {
    auto client = std::make_unique<ClientState>(
      stats, options.seed + static_cast<std::uint32_t>(i));
    client->socket = socketwire_examples::CreateUdpSocket(0);
    if (client->socket == nullptr) {
      stats.connectFailures +=
        static_cast<std::uint64_t>(options.clients - i);
      break;
    }

    client->connection =
      std::make_unique<socketwire::ReliableConnection>(client->socket.get(),
                                                       cfg);
    client->connection->SetHandler(&client->handler);
    if (!client->connection->Connect(server_address, options.port)) {
      stats.connectFailures += 1;
    }
    client->nextConnectAttempt =
      netbench::Clock::now() + std::chrono::milliseconds(250);
    ResetStreams(*client, profile, netbench::nowUs());
    clients.push_back(std::move(client));
  }

  std::println("netbench client created {}/{} real UDP clients",
               clients.size(), options.clients);

  netbench::MetricsWriter metrics(options, "client");
  bool reset_at_measurement = false;
  while (!metrics.done() && !clients.empty()) {
    const auto loop_start = netbench::Clock::now();

    for (auto& client : clients) {
      DrainSocket(*client);
      if (!client->handler.connected &&
          loop_start >= client->nextConnectAttempt) {
        if (!client->connection->Connect(server_address, options.port)) {
          stats.connectFailures += 1;
        }
        client->nextConnectAttempt =
          loop_start + std::chrono::milliseconds(250);
      }
      client->connection->Update();
    }

    if (metrics.measuring() && !reset_at_measurement) {
      const auto now_us = netbench::nowUs();
      for (auto& client : clients) ResetStreams(*client, profile, now_us);
      reset_at_measurement = true;
    }

    if (metrics.measuring()) {
      for (auto& client : clients) {
        if (!client->handler.connected) continue;
        SendDue(*client, client->streams.at(0),
                netbench::DeliveryMode::kReliable, options, stats);
        SendDue(*client, client->streams.at(1),
                netbench::DeliveryMode::kUnreliable, options, stats);
        SendDue(*client, client->streams.at(2),
                netbench::DeliveryMode::kUnsequenced, options, stats);
        SendDue(*client, client->streams.at(3),
                netbench::DeliveryMode::kSequenced, options, stats);
        SendDeadlineDue(*client, client->streams.at(4), options, stats);
      }
    }

    metrics.maybeWriteSample(
      stats, {.clientsRequested = options.clients,
              .clientsCreated = static_cast<int>(clients.size()),
              .connectedClients = ConnectedClients(clients),
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

  std::string_view status = "ok";
  if (clients.empty()) {
    status = "no_clients";
  } else if (static_cast<int>(clients.size()) < options.clients) {
    status = "partial";
  }

  metrics.finish(stats, {.clientsRequested = options.clients,
                         .clientsCreated = static_cast<int>(clients.size()),
                         .connectedClients = ConnectedClients(clients),
                         .status = status,
                         .transport = TransportStats(clients)});
  return 0;
}

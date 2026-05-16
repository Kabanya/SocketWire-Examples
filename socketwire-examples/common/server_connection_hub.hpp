#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "i_socket.hpp"
#include "reliable_connection.hpp"

namespace socketwire_examples {

class ServerConnectionHub {
 public:
  struct Client {
    socketwire::SocketAddress address{};
    std::uint16_t port = 0;
    std::unique_ptr<socketwire::ReliableConnection> connection = nullptr;
    void* userData = nullptr;
  };

  using ConnectedCallback = std::function<void(Client&)>;
  using DisconnectedCallback = std::function<void(Client&)>;
  using PacketCallback =
    std::function<void(Client&, std::uint8_t, const void*, std::size_t, bool)>;

  ServerConnectionHub(socketwire::ISocket* socket,
                      socketwire::ReliableConnectionConfig cfg)
      : socket_(socket), config_(cfg) {}

  void SetConnectedCallback(ConnectedCallback callback) {
    onConnected_ = std::move(callback);
  }
  void SetDisconnectedCallback(DisconnectedCallback callback) {
    onDisconnected_ = std::move(callback);
  }
  void SetPacketCallback(PacketCallback callback) {
    onPacket_ = std::move(callback);
  }

  void Poll() {
    while (true) {
      socketwire::SocketAddress from_addr{};
      std::uint16_t from_port = 0;
      std::uint8_t buffer[4096]{};

      auto result =
        socket_->Receive(buffer, sizeof(buffer), from_addr, from_port);
      if (result.Failed()) break;
      if (result.bytes <= 0) continue;

      auto* client = FindClient(from_addr, from_port);
      if (client == nullptr) {
        if (!IsConnectPacket(buffer, static_cast<std::size_t>(result.bytes))) {
          continue;
        }
        client = CreateClient(from_addr, from_port);
      }

      client->connection->ProcessPacket(
        buffer, static_cast<std::size_t>(result.bytes), from_addr, from_port);
    }
  }

  void Update() {
    for (auto& client : clients_) {
      if (client->connection != nullptr) client->connection->Update();
    }

    std::erase_if(clients_, [this](const auto& client) {
      if (client->connection == nullptr ||
          client->connection->GetState() !=
            socketwire::ConnectionState::kDisconnected) {
        return false;
      }

      clientMap_.erase(MakeKey(client->address, client->port));
      return true;
    });
  }

  std::vector<Client*> Clients() {
    std::vector<Client*> result;
    result.reserve(clients_.size());
    for (auto& client : clients_) result.push_back(client.get());
    return result;
  }

  Client* FindClient(const socketwire::SocketAddress& address,
                     std::uint16_t port) {
    auto it = clientMap_.find(MakeKey(address, port));
    return it == clientMap_.end() ? nullptr : it->second;
  }

 private:
  class ClientHandler final : public socketwire::IReliableConnectionHandler {
   public:
    ClientHandler(ServerConnectionHub& hub, Client& client)
        : hub_(&hub), client_(&client) {}

    void OnConnected() override {
      if (hub_->onConnected_ != nullptr) hub_->onConnected_(*client_);
    }

    void OnDisconnected() override {
      if (hub_->onDisconnected_ != nullptr) hub_->onDisconnected_(*client_);
    }

    void OnReliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
      if (hub_->onPacket_ != nullptr) {
        hub_->onPacket_(*client_, channel, data, size, true);
      }
    }

    void OnUnreliableReceived(std::uint8_t channel, const void* data,
                              std::size_t size) override {
      if (hub_->onPacket_ != nullptr) {
        hub_->onPacket_(*client_, channel, data, size, false);
      }
    }

    void OnTimeout() override {
      if (hub_->onDisconnected_ != nullptr) hub_->onDisconnected_(*client_);
    }

   private:
    ServerConnectionHub* hub_ = nullptr;
    Client* client_ = nullptr;
  };

  struct ClientRecord : Client {
    std::unique_ptr<ClientHandler> handler = nullptr;
  };

  struct ConnectionKey {
    bool isIPv6 = false;
    std::uint16_t port = 0;
    std::uint32_t ipv4 = 0;
    std::array<std::uint8_t, 16> ipv6{};
    std::uint32_t scopeId = 0;

    bool operator==(const ConnectionKey& other) const {
      return isIPv6 == other.isIPv6 && port == other.port &&
             ipv4 == other.ipv4 && ipv6 == other.ipv6 &&
             scopeId == other.scopeId;
    }
  };

  struct ConnectionKeyHash {
    std::size_t operator()(const ConnectionKey& key) const {
      std::size_t h = std::hash<std::uint16_t>{}(key.port);
      h ^= std::hash<bool>{}(key.isIPv6) + 0x9e3779b97f4a7c15ULL + (h << 6) +
           (h >> 2);
      if (key.isIPv6) {
        for (const auto byte : key.ipv6) {
          h ^= std::hash<std::uint8_t>{}(byte) + 0x9e3779b97f4a7c15ULL +
               (h << 6) + (h >> 2);
        }
        h ^= std::hash<std::uint32_t>{}(key.scopeId) + 0x9e3779b97f4a7c15ULL +
             (h << 6) + (h >> 2);
      } else {
        h ^= std::hash<std::uint32_t>{}(key.ipv4) + 0x9e3779b97f4a7c15ULL +
             (h << 6) + (h >> 2);
      }
      return h;
    }
  };

  Client* CreateClient(const socketwire::SocketAddress& address,
                       std::uint16_t port) {
    auto record = std::make_unique<ClientRecord>();
    record->address = address;
    record->port = port;
    record->handler = std::make_unique<ClientHandler>(*this, *record);
    ResetClient(*record);

    Client* raw = record.get();
    clientMap_[MakeKey(address, port)] = raw;
    clients_.push_back(std::move(record));
    return raw;
  }

  void ResetClient(ClientRecord& client) {
    client.connection =
      std::make_unique<socketwire::ReliableConnection>(socket_, config_);
    client.connection->SetRemoteAddress(client.address, client.port);
    client.connection->SetHandler(client.handler.get());
  }

  static bool IsConnectPacket(const void* data, std::size_t size) {
    if (size < 1) return false;
    const auto packet_type = *static_cast<const std::uint8_t*>(data);
    return packet_type ==
           static_cast<std::uint8_t>(socketwire::PacketType::kConnect);
  }

  static ConnectionKey MakeKey(const socketwire::SocketAddress& address,
                               std::uint16_t port) {
    ConnectionKey key;
    key.isIPv6 = address.isIPv6;
    key.port = port;
    if (address.isIPv6) {
      key.ipv6 = address.ipv6.bytes;
      key.scopeId = address.ipv6.scopeId;
    } else {
      key.ipv4 = address.ipv4.hostOrderAddress;
    }
    return key;
  }

  socketwire::ISocket* socket_ = nullptr;
  socketwire::ReliableConnectionConfig config_{};
  std::vector<std::unique_ptr<ClientRecord>> clients_{};
  std::unordered_map<ConnectionKey, Client*, ConnectionKeyHash> clientMap_{};
  ConnectedCallback onConnected_{};
  DisconnectedCallback onDisconnected_{};
  PacketCallback onPacket_{};
};

}  // namespace socketwire_examples

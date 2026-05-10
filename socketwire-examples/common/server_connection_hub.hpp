#pragma once

#include "i_socket.hpp"
#include "reliable_connection.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace socketwire_examples
{

class ServerConnectionHub
{
public:
  struct Client
  {
    socketwire::SocketAddress address{};
    std::uint16_t port = 0;
    std::unique_ptr<socketwire::ReliableConnection> connection;
    void* userData = nullptr;
  };

  using ConnectedCallback = std::function<void(Client&)>;
  using DisconnectedCallback = std::function<void(Client&)>;
  using PacketCallback = std::function<void(Client&, std::uint8_t, const void*, std::size_t, bool)>;

  ServerConnectionHub(socketwire::ISocket* socket, socketwire::ReliableConnectionConfig cfg)
    : socket_(socket)
    , config_(cfg)
  {
  }

  void setConnectedCallback(ConnectedCallback callback) { onConnected_ = std::move(callback); }
  void setDisconnectedCallback(DisconnectedCallback callback) { onDisconnected_ = std::move(callback); }
  void setPacketCallback(PacketCallback callback) { onPacket_ = std::move(callback); }

  void poll()
  {
    while (true)
    {
      socketwire::SocketAddress fromAddr{};
      std::uint16_t fromPort = 0;
      std::uint8_t buffer[4096]{};

      auto result = socket_->Receive(buffer, sizeof(buffer), fromAddr, fromPort);
      if (result.Failed())
        break;
      if (result.bytes <= 0)
        continue;

      auto* client = findClient(fromAddr, fromPort);
      if (client == nullptr)
      {
        if (!isConnectPacket(buffer, static_cast<std::size_t>(result.bytes)))
          continue;
        client = createClient(fromAddr, fromPort);
      }
      else if (isConnectPacket(buffer, static_cast<std::size_t>(result.bytes)) &&
               client->connection != nullptr &&
               client->connection->IsConnected())
      {
        resetClient(*static_cast<ClientRecord*>(client));
      }

      client->connection->ProcessPacket(
        buffer, static_cast<std::size_t>(result.bytes), fromAddr, fromPort);
    }
  }

  void update()
  {
    for (auto& client : clients_)
    {
      if (client->connection != nullptr)
        client->connection->Update();
    }

    std::erase_if(clients_, [this](const auto& client)
    {
      if (client->connection == nullptr ||
          client->connection->GetState() != socketwire::ConnectionState::kDisconnected)
        return false;

      clientMap_.erase(makeKey(client->address, client->port));
      return true;
    });
  }

  std::vector<Client*> clients()
  {
    std::vector<Client*> result;
    result.reserve(clients_.size());
    for (auto& client : clients_)
      result.push_back(client.get());
    return result;
  }

  Client* findClient(const socketwire::SocketAddress& address, std::uint16_t port)
  {
    auto it = clientMap_.find(makeKey(address, port));
    return it == clientMap_.end() ? nullptr : it->second;
  }

private:
  class ClientHandler final : public socketwire::IReliableConnectionHandler
  {
  public:
    ClientHandler(ServerConnectionHub& hub, Client& client)
      : hub_(hub)
      , client_(client)
    {
    }

    void OnConnected() override
    {
      if (hub_.onConnected_ != nullptr)
        hub_.onConnected_(client_);
    }

    void OnDisconnected() override
    {
      if (hub_.onDisconnected_ != nullptr)
        hub_.onDisconnected_(client_);
    }

    void OnReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
    {
      if (hub_.onPacket_ != nullptr)
        hub_.onPacket_(client_, channel, data, size, true);
    }

    void OnUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
    {
      if (hub_.onPacket_ != nullptr)
        hub_.onPacket_(client_, channel, data, size, false);
    }

    void OnTimeout() override
    {
      if (hub_.onDisconnected_ != nullptr)
        hub_.onDisconnected_(client_);
    }

  private:
    ServerConnectionHub& hub_;
    Client& client_;
  };

  struct ClientRecord : Client
  {
    std::unique_ptr<ClientHandler> handler;
  };

  Client* createClient(const socketwire::SocketAddress& address, std::uint16_t port)
  {
    auto record = std::make_unique<ClientRecord>();
    record->address = address;
    record->port = port;
    record->handler = std::make_unique<ClientHandler>(*this, *record);
    resetClient(*record);

    Client* raw = record.get();
    clientMap_[makeKey(address, port)] = raw;
    clients_.push_back(std::move(record));
    return raw;
  }

  void resetClient(ClientRecord& client)
  {
    client.connection = std::make_unique<socketwire::ReliableConnection>(socket_, config_);
    client.connection->SetRemoteAddress(client.address, client.port);
    client.connection->SetHandler(client.handler.get());
  }

  static bool isConnectPacket(const void* data, std::size_t size)
  {
    if (size < 1)
      return false;
    const auto packetType = *static_cast<const std::uint8_t*>(data);
    return packetType == static_cast<std::uint8_t>(socketwire::PacketType::kConnect);
  }

  static std::string makeKey(const socketwire::SocketAddress& address, std::uint16_t port)
  {
    return socketwire::MakeConnectionKey(address, port);
  }

  socketwire::ISocket* socket_ = nullptr;
  socketwire::ReliableConnectionConfig config_{};
  std::vector<std::unique_ptr<ClientRecord>> clients_;
  std::unordered_map<std::string, Client*> clientMap_;
  ConnectedCallback onConnected_;
  DisconnectedCallback onDisconnected_;
  PacketCallback onPacket_;
};

} // namespace socketwire_examples

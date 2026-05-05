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

  void onReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    const auto typeValue = stream.try_read<std::uint8_t>();
    if (!typeValue)
      return;

    const auto type = static_cast<channels_demo::MessageType>(*typeValue);
    if (type == channels_demo::MessageType::CommandAck)
    {
      const auto commandId = stream.try_read<std::uint32_t>();
      if (commandId)
        std::printf("channel %u reliable ack for command #%u\n", channel, *commandId);
      return;
    }

    if (type == channels_demo::MessageType::Snapshot)
    {
      const auto tick = stream.try_read<std::uint32_t>();
      const auto x = stream.try_read<float>();
      const auto y = stream.try_read<float>();
      if (tick && x && y)
      {
        ++snapshots;
        std::printf("channel %u unsequenced snapshot tick=%u x=%.2f y=%.2f\n",
                    channel,
                    *tick,
                    *x,
                    *y);
      }
    }
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    onReliableReceived(channel, data, size);
  }

  bool connected = false;
  int snapshots = 0;
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
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(SocketConstants::loopback(), channels_demo::kPort);

  const auto started = std::chrono::steady_clock::now();
  auto lastMove = started;
  bool commandSent = false;
  std::uint32_t tick = 0;

  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
  {
    connection.tick();

    if (handler.connected && !commandSent)
    {
      auto command = channels_demo::make_command(1, "open-door");
      connection.sendReliable(0, command);
      commandSent = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (handler.connected && now - lastMove > std::chrono::milliseconds(80))
    {
      lastMove = now;
      const float x = static_cast<float>(tick) * 0.25f;
      const float y = 10.0f + static_cast<float>(tick % 4);
      auto movement = channels_demo::make_movement(tick++, x, y);
      connection.sendUnreliable(1, movement);
    }

    if (handler.snapshots >= 5)
      break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf("channels-demo client finished with %d snapshots\n", handler.snapshots);
  return handler.snapshots > 0 ? 0 : 1;
}

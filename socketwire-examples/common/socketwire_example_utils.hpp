#pragma once

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace socketwire_examples
{

inline std::unique_ptr<socketwire::ISocket> createUdpSocket(std::uint16_t port)
{
  if (!socketwire::initialize_sockets())
  {
    std::printf("Cannot init SocketWire\n");
    return nullptr;
  }

  auto* factory = socketwire::SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot get socket factory\n");
    return nullptr;
  }

  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->createUDPSocket(cfg);
  if (socket == nullptr)
  {
    std::printf("Cannot create UDP socket\n");
    return nullptr;
  }

  if (socket->bind(socketwire::SocketConstants::any(), port) != socketwire::SocketError::None)
  {
    std::printf("Cannot bind UDP socket to port %u\n", port);
    return nullptr;
  }

  return socket;
}

inline socketwire::SocketAddress resolveAddress(const std::string& host)
{
  if (host == "localhost")
    return socketwire::SocketConstants::loopback();

  const std::optional<socketwire::SocketAddress> parsed =
    socketwire::SocketConstants::tryFromString(host.c_str());
  return parsed.value_or(socketwire::SocketConstants::loopback());
}

inline void writeString(socketwire::BitStream& stream, const std::string& value)
{
  stream.writeBytes(value.c_str(), value.size() + 1);
}

inline std::string readStringPayload(const void* data, std::size_t size)
{
  if (data == nullptr || size == 0)
    return {};

  const char* text = static_cast<const char*>(data);
  std::size_t length = 0;
  while (length < size && text[length] != '\0')
    ++length;
  return std::string(text, length);
}

} // namespace socketwire_examples

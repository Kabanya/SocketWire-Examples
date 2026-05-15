#pragma once

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace socketwire_examples
{

inline std::optional<std::uint16_t> parsePort(const char* text)
{
  if (text == nullptr || *text == '\0')
    return std::nullopt;

  char* end = nullptr;
  errno = 0;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535UL)
    return std::nullopt;

  return static_cast<std::uint16_t>(value);
}

inline std::uint16_t portFromArgsOrEnv(int argc,
                                       const char** argv,
                                       int argIndex,
                                       const char* envName,
                                       std::uint16_t defaultPort)
{
  std::uint16_t port = defaultPort;

  if (envName != nullptr)
  {
    const char* envValue = std::getenv(envName);
    if (envValue != nullptr && *envValue != '\0')
    {
      const std::optional<std::uint16_t> parsed = parsePort(envValue);
      if (parsed.has_value())
        port = *parsed;
      else
        std::printf("Ignoring invalid port in %s='%s'; using %u\n",
                    envName,
                    envValue,
                    static_cast<unsigned>(port));
    }
  }

  if (argIndex > 0 && argIndex < argc && argv != nullptr && argv[argIndex] != nullptr)
  {
    const std::optional<std::uint16_t> parsed = parsePort(argv[argIndex]);
    if (parsed.has_value())
      port = *parsed;
    else
      std::printf("Ignoring invalid port argument '%s'; using %u\n",
                  argv[argIndex],
                  static_cast<unsigned>(port));
  }

  return port;
}

inline std::unique_ptr<socketwire::ISocket> createUdpSocket(std::uint16_t port)
{
  socketwire::InitializeSockets();

  auto* factory = socketwire::SocketFactoryRegistry::GetFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot get socket factory\n");
    return nullptr;
  }

  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->CreateUdpSocket(cfg);
  if (socket == nullptr)
  {
    std::printf("Cannot create UDP socket\n");
    return nullptr;
  }

  if (socket->Bind(socketwire::SocketConstants::Any(), port) != socketwire::SocketError::kNone)
  {
    std::printf("Cannot bind UDP socket to port %u\n", static_cast<unsigned>(port));
    return nullptr;
  }

  return socket;
}

inline socketwire::SocketAddress resolveAddress(const std::string& host)
{
  if (host == "localhost")
    return socketwire::SocketConstants::Loopback();

  const std::optional<socketwire::SocketAddress> parsed =
    socketwire::SocketConstants::TryFromString(host.c_str());
  return parsed.value_or(socketwire::SocketConstants::Loopback());
}

inline void writeString(socketwire::BitStream& stream, const std::string& value)
{
  stream.WriteBytes(value.c_str(), value.size() + 1);
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

#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

namespace socketwire_examples {

inline std::optional<std::uint16_t> ParsePort(const char* text) {
  if (text == nullptr || *text == '\0') return std::nullopt;

  const std::string_view view(text);
  std::uint64_t value = 0;
  const auto [end, error] =
    std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc{} || end != view.data() + view.size() || value == 0 ||
      value > 65535U) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(value);
}

inline std::uint16_t PortFromArgsOrEnv(int argc, const char** argv,
                                       int arg_index, const char* env_name,
                                       std::uint16_t default_port) {
  std::uint16_t port = default_port;

  if (env_name != nullptr) {
    const char* env_value = std::getenv(env_name);
    if (env_value != nullptr && *env_value != '\0') {
      const std::optional<std::uint16_t> parsed = ParsePort(env_value);
      if (parsed.has_value()) {
        port = *parsed;
      } else {
        std::println("Ignoring invalid port in {}='{}'; using {}", env_name,
                     env_value, static_cast<unsigned>(port));
      }
    }
  }

  if (arg_index > 0 && arg_index < argc && argv != nullptr &&
      argv[arg_index] != nullptr) {
    const std::optional<std::uint16_t> parsed = ParsePort(argv[arg_index]);
    if (parsed.has_value()) {
      port = *parsed;
    } else {
      std::println("Ignoring invalid port argument '{}'; using {}",
                   argv[arg_index], static_cast<unsigned>(port));
    }
  }

  return port;
}

inline std::unique_ptr<socketwire::ISocket> CreateUdpSocket(
  std::uint16_t port) {
  socketwire::InitializeSockets();

  auto* factory = socketwire::SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Cannot get socket factory");
    return nullptr;
  }

  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->CreateUdpSocket(cfg);
  if (socket == nullptr) {
    std::println("Cannot create UDP socket");
    return nullptr;
  }

  if (socket->Bind(socketwire::SocketConstants::Any(), port) !=
      socketwire::SocketError::kNone) {
    std::println("Cannot bind UDP socket to port {}",
                 static_cast<unsigned>(port));
    return nullptr;
  }

  return socket;
}

inline socketwire::SocketAddress ResolveAddress(const std::string& host) {
  if (host == "localhost") return socketwire::SocketConstants::Loopback();

  const std::optional<socketwire::SocketAddress> parsed =
    socketwire::SocketConstants::TryFromString(host.c_str());
  return parsed.value_or(socketwire::SocketConstants::Loopback());
}

inline void WriteString(socketwire::BitStream& stream,
                        const std::string& value) {
  stream.WriteBytes(value.c_str(), value.size() + 1);
}

inline std::string ReadStringPayload(const void* data, std::size_t size) {
  if (data == nullptr || size == 0) return {};

  const char* text = static_cast<const char*>(data);
  std::size_t length = 0;
  while (length < size && text[length] != '\0') ++length;
  return {text, length};
}

}  // namespace socketwire_examples

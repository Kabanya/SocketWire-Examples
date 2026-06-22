#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socket_resolver.hpp"

namespace socketwire_examples {

inline std::optional<std::uint16_t> ParsePort(std::string_view view) {
  if (view.empty()) return std::nullopt;

  std::uint64_t value = 0;
  const auto [end, error] =
    std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc{} || end != view.data() + view.size() || value == 0 ||
      value > 65535U) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(value);
}

inline std::optional<std::uint16_t> ParsePort(const char* text) {
  if (text == nullptr) return std::nullopt;
  return ParsePort(std::string_view(text));
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

inline bool HasCommandLineOption(int argc, const char** argv,
                                 std::string_view option) {
  if (argv == nullptr || option.empty()) return false;

  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::string_view(argv[i]) == option) {
      return true;
    }
  }

  return false;
}

inline bool IsCommandLineOption(const char* arg) {
  return arg != nullptr && arg[0] == '-' && arg[1] != '\0';
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
  cfg.enableIPv6 = false;

  auto bind_socket =
    [&](const socketwire::SocketConfig& socket_config,
        const socketwire::SocketAddress& address)
    -> std::unique_ptr<socketwire::ISocket> {
    auto socket = factory->CreateUdpSocket(socket_config);
    if (socket == nullptr) return nullptr;
    if (socket->Bind(address, port) == socketwire::SocketError::kNone) {
      return socket;
    }
    return nullptr;
  };

  if (auto socket = bind_socket(cfg, socketwire::socket_constants::Any())) {
    return socket;
  }

  cfg.enableIPv6 = true;
  if (auto socket =
        bind_socket(cfg, socketwire::socket_constants::AnyIPv6())) {
    return socket;
  }

  std::println("Cannot bind UDP socket to port {}", static_cast<unsigned>(port));
  return nullptr;
}

struct ResolvedEndpoint {
  std::vector<socketwire::SocketAddress> addresses;
  std::size_t nextAddress = 0;
};

inline std::optional<ResolvedEndpoint> ResolveEndpoint(
  std::string_view host, std::uint16_t port = 0,
  socketwire::AddressFamily family = socketwire::AddressFamily::kAny) {
  if (host.empty()) return std::nullopt;

  const socketwire::ResolveHostResult result =
    socketwire::ResolveHost(host, port, family);
  if (result.Failed() || result.addresses.empty()) return std::nullopt;
  return ResolvedEndpoint{.addresses = result.addresses};
}

inline std::optional<socketwire::SocketAddress> ResolveAddress(
  std::string_view host,
  socketwire::AddressFamily family = socketwire::AddressFamily::kAny) {
  auto endpoint = ResolveEndpoint(host, 0, family);
  if (!endpoint || endpoint->addresses.empty()) return std::nullopt;
  return endpoint->addresses.front();
}

inline bool ConnectNextAddress(socketwire::ReliableConnection& connection,
                               ResolvedEndpoint& endpoint,
                               std::uint16_t port) {
  if (endpoint.addresses.empty()) return false;

  for (std::size_t i = 0; i < endpoint.addresses.size(); ++i) {
    const socketwire::SocketAddress& address =
      endpoint.addresses[endpoint.nextAddress];
    endpoint.nextAddress = (endpoint.nextAddress + 1) % endpoint.addresses.size();
    if (connection.Connect(address, port)) return true;
  }
  return false;
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

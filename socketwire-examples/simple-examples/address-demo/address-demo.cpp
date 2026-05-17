#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <print>
#include <thread>

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

using namespace socketwire;  // NOLINT

static void PrintIpv4(const char* text, std::uint32_t value) {
  char buffer[16]{};
  if (SocketConstants::FormatIPv4(value, buffer, sizeof(buffer))) {
    std::println("{}{}", text, buffer);
  }
}

static void PrintIpv6(const char* text, const SocketAddress& address) {
  if (!address.isIPv6) return;

  const auto value =
    SocketConstants::FormatIPv6String(address.ipv6.bytes, address.ipv6.scopeId);
  std::println("{}{}", text, value);
}

static void RunIpv6LoopbackProbe(ISocketFactory& factory) {
  SocketConfig cfg;
  cfg.enableIPv6 = true;

  auto receiver = factory.CreateUdpSocket(cfg);
  if (receiver == nullptr) {
    std::println("IPv6 probe: cannot create IPv6 UDP socket");
    return;
  }

  const SocketAddress loopback6 = SocketConstants::LoopbackIPv6();
  const auto bind_error = receiver->Bind(loopback6, 0);
  if (bind_error != SocketError::kNone) {
    std::println("IPv6 probe: bind(::1) failed: {}",
                 socketwire::ToString(bind_error));
    return;
  }

  auto sender = factory.CreateUdpSocket(cfg);
  if (sender == nullptr ||
      sender->Bind(SocketConstants::AnyIPv6(), 0) != SocketError::kNone) {
    std::println("IPv6 probe: cannot create sender socket");
    return;
  }

  const char payload[] = "ipv6-loopback";
  const auto sent = sender->SendTo(payload, std::strlen(payload), loopback6,
                                   receiver->LocalPort());
  if (sent.Failed()) {
    std::println("IPv6 probe: send failed: {}",
                 socketwire::ToString(sent.error));
    return;
  }

  for (int i = 0; i < 20; ++i) {
    std::array<std::uint8_t, 128> buffer{};
    SocketAddress from{};
    std::uint16_t from_port = 0;
    const auto received =
      receiver->Receive(buffer.data(), buffer.size(), from, from_port);
    if (received.Succeeded() && received.bytes > 0) {
      std::println("IPv6 probe: received '{:.{}}' from port {}",
                   reinterpret_cast<const char*>(buffer.data()),
                   static_cast<int>(received.bytes), from_port);
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::println("IPv6 probe: no loopback packet received");
}

int main() {
  InitializeSockets();

  std::uint32_t parsed4 = 0;
  if (SocketConstants::ParseIPv4("127.0.0.1", parsed4)) {
    PrintIpv4("parseIPv4(\"127.0.0.1\") -> ", parsed4);
  }

  std::array<std::uint8_t, 16> parsed6{};
  std::uint32_t scope_id = 0;
  if (SocketConstants::ParseIPv6("::1", parsed6, scope_id)) {
    std::println("parseIPv6(\"::1\") -> {}",
                 SocketConstants::FormatIPv6String(parsed6, scope_id));
  }

  if (const auto address = SocketConstants::TryFromString("192.168.10.42")) {
    PrintIpv4("tryFromString IPv4 -> ", address->ipv4.hostOrderAddress);
  }

  if (const auto address = SocketConstants::TryFromString("::1")) {
    PrintIpv6("tryFromString IPv6 -> ", *address);
  }

  PrintIpv6("SocketConstants::LoopbackIPv6() -> ",
            SocketConstants::LoopbackIPv6());

  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Socket factory not initialized");
    return 1;
  }

  SocketConfig dual_stack;
  dual_stack.enableIPv6 = true;
  auto socket = factory->CreateUdpSocket(dual_stack);
  if (socket != nullptr &&
      socket->Bind(SocketConstants::AnyIPv6(), 0) == SocketError::kNone) {
    std::println("Dual-stack-style UDP socket bound on port {}",
                 socket->LocalPort());
  } else {
    std::println("Dual-stack-style UDP socket is unavailable on this host");
  }

  RunIpv6LoopbackProbe(*factory);
  return 0;
}

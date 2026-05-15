#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace socketwire; // NOLINT

static void print_ipv4(const char* text, std::uint32_t value)
{
  char buffer[16]{};
  if (SocketConstants::FormatIPv4(value, buffer, sizeof(buffer)))
    std::printf("%s%s\n", text, buffer);
}

static void print_ipv6(const char* text, const SocketAddress& address)
{
  if (!address.isIPv6)
    return;

  const auto value = SocketConstants::FormatIPv6String(address.ipv6.bytes, address.ipv6.scopeId);
  std::printf("%s%s\n", text, value.c_str());
}

static void run_ipv6_loopback_probe(ISocketFactory& factory)
{
  SocketConfig cfg;
  cfg.enableIPv6 = true;

  auto receiver = factory.CreateUdpSocket(cfg);
  if (receiver == nullptr)
  {
    std::printf("IPv6 probe: cannot create IPv6 UDP socket\n");
    return;
  }

  const SocketAddress loopback6 = SocketConstants::LoopbackIPv6();
  const auto bindError = receiver->Bind(loopback6, 0);
  if (bindError != SocketError::kNone)
  {
    std::printf("IPv6 probe: bind(::1) failed: %s\n", socketwire::ToString(bindError));
    return;
  }

  auto sender = factory.CreateUdpSocket(cfg);
  if (sender == nullptr || sender->Bind(SocketConstants::AnyIPv6(), 0) != SocketError::kNone)
  {
    std::printf("IPv6 probe: cannot create sender socket\n");
    return;
  }

  const char payload[] = "ipv6-loopback";
  const auto sent = sender->SendTo(payload, std::strlen(payload), loopback6, receiver->LocalPort());
  if (sent.Failed())
  {
    std::printf("IPv6 probe: send failed: %s\n", socketwire::ToString(sent.error));
    return;
  }

  for (int i = 0; i < 20; ++i)
  {
    std::array<std::uint8_t, 128> buffer{};
    SocketAddress from{};
    std::uint16_t fromPort = 0;
    const auto received = receiver->Receive(buffer.data(), buffer.size(), from, fromPort);
    if (received.Succeeded() && received.bytes > 0)
    {
      std::printf("IPv6 probe: received '%.*s' from port %u\n",
                  static_cast<int>(received.bytes),
                  reinterpret_cast<const char*>(buffer.data()),
                  fromPort);
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::printf("IPv6 probe: no loopback packet received\n");
}

int main()
{
  InitializeSockets();

  std::uint32_t parsed4 = 0;
  if (SocketConstants::ParseIPv4("127.0.0.1", parsed4))
    print_ipv4("parseIPv4(\"127.0.0.1\") -> ", parsed4);

  std::array<std::uint8_t, 16> parsed6{};
  std::uint32_t scopeId = 0;
  if (SocketConstants::ParseIPv6("::1", parsed6, scopeId))
    std::printf("parseIPv6(\"::1\") -> %s\n",
                SocketConstants::FormatIPv6String(parsed6, scopeId).c_str());

  if (const auto address = SocketConstants::TryFromString("192.168.10.42"))
    print_ipv4("tryFromString IPv4 -> ", address->ipv4.hostOrderAddress);

  if (const auto address = SocketConstants::TryFromString("::1"))
    print_ipv6("tryFromString IPv6 -> ", *address);

  print_ipv6("SocketConstants::LoopbackIPv6() -> ", SocketConstants::LoopbackIPv6());

  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr)
  {
    std::printf("Socket factory not initialized\n");
    return 1;
  }

  SocketConfig dualStack;
  dualStack.enableIPv6 = true;
  auto socket = factory->CreateUdpSocket(dualStack);
  if (socket != nullptr && socket->Bind(SocketConstants::AnyIPv6(), 0) == SocketError::kNone)
    std::printf("Dual-stack-style UDP socket bound on port %u\n", socket->LocalPort());
  else
    std::printf("Dual-stack-style UDP socket is unavailable on this host\n");

  run_ipv6_loopback_probe(*factory);
  return 0;
}

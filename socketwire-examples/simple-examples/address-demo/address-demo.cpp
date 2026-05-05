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
  if (SocketConstants::formatIPv4(value, buffer, sizeof(buffer)))
    std::printf("%s%s\n", text, buffer);
}

static void print_ipv6(const char* text, const SocketAddress& address)
{
  if (!address.isIPv6)
    return;

  const auto value = SocketConstants::formatIPv6String(address.ipv6.bytes, address.ipv6.scopeId);
  std::printf("%s%s\n", text, value.c_str());
}

static void run_ipv6_loopback_probe(ISocketFactory& factory)
{
  SocketConfig cfg;
  cfg.enableIPv6 = true;

  auto receiver = factory.createUDPSocket(cfg);
  if (receiver == nullptr)
  {
    std::printf("IPv6 probe: cannot create IPv6 UDP socket\n");
    return;
  }

  const SocketAddress loopback6 = SocketConstants::loopbackIPv6();
  const auto bindError = receiver->bind(loopback6, 0);
  if (bindError != SocketError::None)
  {
    std::printf("IPv6 probe: bind(::1) failed: %s\n", to_string(bindError));
    return;
  }

  auto sender = factory.createUDPSocket(cfg);
  if (sender == nullptr || sender->bind(SocketConstants::anyIPv6(), 0) != SocketError::None)
  {
    std::printf("IPv6 probe: cannot create sender socket\n");
    return;
  }

  const char payload[] = "ipv6-loopback";
  const auto sent = sender->sendTo(payload, std::strlen(payload), loopback6, receiver->localPort());
  if (sent.failed())
  {
    std::printf("IPv6 probe: send failed: %s\n", to_string(sent.error));
    return;
  }

  for (int i = 0; i < 20; ++i)
  {
    std::array<std::uint8_t, 128> buffer{};
    SocketAddress from{};
    std::uint16_t fromPort = 0;
    const auto received = receiver->receive(buffer.data(), buffer.size(), from, fromPort);
    if (received.succeeded() && received.bytes > 0)
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
  initialize_sockets();

  std::uint32_t parsed4 = 0;
  if (SocketConstants::parseIPv4("127.0.0.1", parsed4))
    print_ipv4("parseIPv4(\"127.0.0.1\") -> ", parsed4);

  std::array<std::uint8_t, 16> parsed6{};
  std::uint32_t scopeId = 0;
  if (SocketConstants::parseIPv6("::1", parsed6, scopeId))
    std::printf("parseIPv6(\"::1\") -> %s\n",
                SocketConstants::formatIPv6String(parsed6, scopeId).c_str());

  if (const auto address = SocketConstants::tryFromString("192.168.10.42"))
    print_ipv4("tryFromString IPv4 -> ", address->ipv4.hostOrderAddress);

  if (const auto address = SocketConstants::tryFromString("::1"))
    print_ipv6("tryFromString IPv6 -> ", *address);

  print_ipv6("SocketConstants::loopbackIPv6() -> ", SocketConstants::loopbackIPv6());

  auto* factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::printf("Socket factory not initialized\n");
    return 1;
  }

  SocketConfig dualStack;
  dualStack.enableIPv6 = true;
  auto socket = factory->createUDPSocket(dualStack);
  if (socket != nullptr && socket->bind(SocketConstants::anyIPv6(), 0) == SocketError::None)
    std::printf("Dual-stack-style UDP socket bound on port %u\n", socket->localPort());
  else
    std::printf("Dual-stack-style UDP socket is unavailable on this host\n");

  run_ipv6_loopback_probe(*factory);
  return 0;
}

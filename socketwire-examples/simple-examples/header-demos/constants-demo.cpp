#include <cstring>
#include <iomanip>
#include <iostream>

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

using namespace socketwire;  // NOLINT

void PrintSectionHeader(const char* title) {
  std::cout << "\n" << std::string(60, '=') << '\n';
  std::cout << "  " << title << '\n';
  std::cout << std::string(60, '=') << '\n';
}

void DemoConstants() {
  PrintSectionHeader("IPv4 Address Constants");

  std::cout << "IPV4_ANY:       0x" << std::hex << std::setw(8)
            << std::setfill('0') << socket_constants::kIpV4Any << std::dec
            << '\n';
  std::cout << "IPV4_LOOPBACK:  0x" << std::hex << std::setw(8)
            << std::setfill('0') << socket_constants::kIpV4Loopback << std::dec
            << '\n';
  std::cout << "IPV4_BROADCAST: 0x" << std::hex << std::setw(8)
            << std::setfill('0') << socket_constants::kIpV4Broadcast << std::dec
            << '\n';
  std::cout << "PORT_ANY:       " << socket_constants::kPortAny << '\n';
}

void DemoFactoryMethods() {
  PrintSectionHeader("Factory Methods");

  SocketAddress const any_addr = socket_constants::Any();
  SocketAddress const loopback_addr = socket_constants::Loopback();
  SocketAddress const broadcast_addr = socket_constants::Broadcast();

  std::cout << "socket_constants::Any()       -> 0x" << std::hex << std::setw(8)
            << std::setfill('0') << any_addr.ipv4.hostOrderAddress << std::dec
            << '\n';
  std::cout << "socket_constants::Loopback()  -> 0x" << std::hex << std::setw(8)
            << std::setfill('0') << loopback_addr.ipv4.hostOrderAddress
            << std::dec << '\n';
  std::cout << "socket_constants::Broadcast() -> 0x" << std::hex << std::setw(8)
            << std::setfill('0') << broadcast_addr.ipv4.hostOrderAddress
            << std::dec << '\n';
}

void DemoFromOctets() {
  PrintSectionHeader("Creating Addresses from Octets");

  SocketAddress const addr1 = socket_constants::FromOctets(192, 168, 1, 1);
  SocketAddress const addr2 = socket_constants::FromOctets(10, 0, 0, 1);
  SocketAddress const addr3 = socket_constants::FromOctets(172, 16, 254, 100);

  char buffer[16];

  socket_constants::FormatIPv4(addr1.ipv4.hostOrderAddress, buffer,
                              sizeof(buffer));
  std::cout << "fromOctets(192, 168, 1, 1)   -> " << buffer << '\n';

  socket_constants::FormatIPv4(addr2.ipv4.hostOrderAddress, buffer,
                              sizeof(buffer));
  std::cout << "fromOctets(10, 0, 0, 1)      -> " << buffer << '\n';

  socket_constants::FormatIPv4(addr3.ipv4.hostOrderAddress, buffer,
                              sizeof(buffer));
  std::cout << "fromOctets(172, 16, 254, 100) -> " << buffer << '\n';
}

void DemoParseIPv4() {
  PrintSectionHeader("Parsing IPv4 Addresses from Strings");

  const char* test_addresses[] = {"192.168.1.100", "127.0.0.1",
                                  "8.8.8.8",       "255.255.255.255",
                                  "invalid",       "999.999.999.999"};

  for (const char* addr_str : test_addresses) {
    std::uint32_t addr = 0;
    bool const success = socket_constants::ParseIPv4(addr_str, addr);

    std::cout << std::left << std::setw(20) << addr_str << " -> ";
    if (success) {
      std::cout << "SUCCESS (0x" << std::hex << std::setw(8)
                << std::setfill('0') << addr << std::dec << ")" << '\n';
    } else {
      std::cout << "FAILED" << '\n';
    }
  }
}

void DemoFormatIPv4() {
  PrintSectionHeader("Converting Addresses to Strings");

  std::uint32_t const addresses[] = {
    0x7F000001,  // 127.0.0.1
    0xC0A80101,  // 192.168.1.1
    0x08080808,  // 8.8.8.8
    0xFFFFFFFF,  // 255.255.255.255
    0x00000000   // 0.0.0.0
  };

  char buffer[16];

  for (std::uint32_t addr : addresses) {
    std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << addr
              << std::dec << " -> ";

    if (socket_constants::FormatIPv4(addr, buffer, sizeof(buffer))) {
      std::cout << buffer << '\n';
    } else {
      std::cout << "ERROR" << '\n';
    }
  }
}

void DemoFromString() {
  PrintSectionHeader("Creating SocketAddress from String");

  const char* addresses[] = {"192.168.1.1", "10.0.0.1", "127.0.0.1", "8.8.8.8"};

  char buffer[16];

  for (const char* addr_str : addresses) {
    SocketAddress const addr = socket_constants::FromString(addr_str);
    socket_constants::FormatIPv4(addr.ipv4.hostOrderAddress, buffer,
                                sizeof(buffer));

    std::cout << std::left << std::setw(20) << addr_str << " -> " << buffer
              << '\n';
  }
}

void DemoRealWorldUsage() {
  PrintSectionHeader("Real-World Usage Example");

  std::cout << "\nCreating a UDP socket and binding to all interfaces...\n"
            << '\n';

  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::cerr << "ERROR: Socket factory not initialized!" << '\n';
    return;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (!socket) {
    std::cerr << "ERROR: Failed to create socket!" << '\n';
    return;
  }

  // Bind to any interface on port 0 (OS chooses port)
  SocketAddress const bind_addr = socket_constants::Any();
  SocketError const bind_result =
    socket->Bind(bind_addr, socket_constants::kPortAny);

  if (bind_result != SocketError::kNone) {
    std::cerr << "ERROR: Failed to bind socket!" << '\n';
    return;
  }

  std::uint16_t local_port = socket->LocalPort();

  char buffer[16];
  socket_constants::FormatIPv4(bind_addr.ipv4.hostOrderAddress, buffer,
                              sizeof(buffer));

  std::cout << "✓ Socket created successfully" << '\n';
  std::cout << "✓ Bound to address: " << buffer << '\n';
  std::cout << "✓ Bound to port:    " << local_port << '\n';

  // Demonstrate sending to localhost
  std::cout << "\nPreparing to send to localhost:" << local_port << "..."
            << '\n';

  SocketAddress const dest_addr = socket_constants::Loopback();
  socket_constants::FormatIPv4(dest_addr.ipv4.hostOrderAddress, buffer,
                              sizeof(buffer));

  std::cout << "✓ Destination: " << buffer << ":" << local_port << '\n';

  const char* message = "Hello from SocketConstants demo!";
  SocketResult const send_result =
    socket->SendTo(message, strlen(message), dest_addr, local_port);

  if (send_result.Succeeded()) {
    std::cout << "✓ Sent " << send_result.bytes << " bytes" << '\n';
  } else {
    std::cerr << "✗ Send failed with error: "
              << static_cast<int>(send_result.error) << '\n';
  }

  std::cout << "\nSocket demo completed successfully!" << '\n';
}

void DemoComparison() {
  PrintSectionHeader("Before vs After SocketConstants");

  std::cout << "\n--- BEFORE (Platform-Specific) ---\n" << '\n';
  std::cout << "#if defined(_WIN32)" << '\n';
  std::cout << "#include <winsock2.h>" << '\n';
  std::cout << "#else" << '\n';
  std::cout << "#include <netinet/in.h>" << '\n';
  std::cout << "#endif" << '\n';
  std::cout << "\nSocketAddress addr = SocketAddress::fromIPv4(INADDR_ANY);"
            << '\n';
  std::cout
    << "SocketAddress dest = SocketAddress::fromIPv4(htonl(INADDR_LOOPBACK));"
    << '\n';

  std::cout << "\n--- AFTER (Cross-Platform) ---\n" << '\n';
  std::cout << "#include \"socket_constants.hpp\"" << '\n';
  std::cout << "\nSocketAddress addr = socket_constants::Any();" << '\n';
  std::cout << "SocketAddress dest = socket_constants::Loopback();" << '\n';

  std::cout << "\n✓ Cleaner code" << '\n';
  std::cout << "✓ No platform-specific #ifdef blocks" << '\n';
  std::cout << "✓ No manual byte order conversions" << '\n';
  std::cout << "✓ Same code works on Windows, Linux, and macOS" << '\n';
}

int main() {
  std::cout << "\n";
  std::cout << "╔════════════════════════════════════════════════════════════╗"
            << '\n';
  std::cout << "║                                                            ║"
            << '\n';
  std::cout << "║         SocketWire SocketConstants Demo                   ║"
            << '\n';
  std::cout << "║         Cross-Platform Network Constants                  ║"
            << '\n';
  std::cout << "║                                                            ║"
            << '\n';
  std::cout << "╚════════════════════════════════════════════════════════════╝"
            << '\n';

  // Initialize sockets for real-world demo
  socketwire::InitializeSockets();

  // Run all demonstrations
  DemoConstants();
  DemoFactoryMethods();
  DemoFromOctets();
  DemoParseIPv4();
  DemoFormatIPv4();
  DemoFromString();
  DemoRealWorldUsage();
  DemoComparison();

  PrintSectionHeader("Demo Complete");
  std::cout << "\nAll features demonstrated successfully!" << '\n';
  std::cout << "\nFor more information, see:" << '\n';
  std::cout << "  - docs/SocketConstants.md" << '\n';
  std::cout << "  - docs/MigrationToSocketConstants.md" << '\n';
  std::cout << '\n';

  return 0;
}
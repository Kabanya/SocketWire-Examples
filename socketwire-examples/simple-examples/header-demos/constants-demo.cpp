#include "i_socket.hpp"
#include "socket_init.hpp"
#include "socket_constants.hpp"
#include <iostream>
#include <iomanip>
#include <cstring>

using namespace socketwire; //NOLINT

void printSectionHeader(const char* title)
{
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "  " << title << std::endl;
  std::cout << std::string(60, '=') << std::endl;
}

void demoConstants()
{
  printSectionHeader("IPv4 Address Constants");

  std::cout << "IPV4_ANY:       0x" << std::hex << std::setw(8) << std::setfill('0') 
            << SocketConstants::IPV4_ANY << std::dec << std::endl;
  std::cout << "IPV4_LOOPBACK:  0x" << std::hex << std::setw(8) << std::setfill('0') 
            << SocketConstants::IPV4_LOOPBACK << std::dec << std::endl;
  std::cout << "IPV4_BROADCAST: 0x" << std::hex << std::setw(8) << std::setfill('0') 
            << SocketConstants::IPV4_BROADCAST << std::dec << std::endl;
  std::cout << "PORT_ANY:       " << SocketConstants::PORT_ANY << std::endl;
}

void demoFactoryMethods()
{
  printSectionHeader("Factory Methods");

  SocketAddress anyAddr = SocketConstants::any();
  SocketAddress loopbackAddr = SocketConstants::loopback();
  SocketAddress broadcastAddr = SocketConstants::broadcast();

  std::cout << "SocketConstants::any()       -> 0x" << std::hex << std::setw(8) << std::setfill('0')
            << anyAddr.ipv4.hostOrderAddress << std::dec << std::endl;
  std::cout << "SocketConstants::loopback()  -> 0x" << std::hex << std::setw(8) << std::setfill('0')
            << loopbackAddr.ipv4.hostOrderAddress << std::dec << std::endl;
  std::cout << "SocketConstants::broadcast() -> 0x" << std::hex << std::setw(8) << std::setfill('0')
            << broadcastAddr.ipv4.hostOrderAddress << std::dec << std::endl;
}

void demoFromOctets()
{
  printSectionHeader("Creating Addresses from Octets");

  SocketAddress addr1 = SocketConstants::fromOctets(192, 168, 1, 1);
  SocketAddress addr2 = SocketConstants::fromOctets(10, 0, 0, 1);
  SocketAddress addr3 = SocketConstants::fromOctets(172, 16, 254, 100);

  char buffer[16];

  SocketConstants::formatIPv4(addr1.ipv4.hostOrderAddress, buffer, sizeof(buffer));
  std::cout << "fromOctets(192, 168, 1, 1)   -> " << buffer << std::endl;

  SocketConstants::formatIPv4(addr2.ipv4.hostOrderAddress, buffer, sizeof(buffer));
  std::cout << "fromOctets(10, 0, 0, 1)      -> " << buffer << std::endl;

  SocketConstants::formatIPv4(addr3.ipv4.hostOrderAddress, buffer, sizeof(buffer));
  std::cout << "fromOctets(172, 16, 254, 100) -> " << buffer << std::endl;
}

void demoParseIPv4()
{
  printSectionHeader("Parsing IPv4 Addresses from Strings");

  const char* testAddresses[] = {
    "192.168.1.100",
    "127.0.0.1",
    "8.8.8.8",
    "255.255.255.255",
    "invalid",
    "999.999.999.999"
  };

  for (const char* addrStr : testAddresses)
  {
    std::uint32_t addr;
    bool success = SocketConstants::parseIPv4(addrStr, addr);

    std::cout << std::left << std::setw(20) << addrStr << " -> ";
    if (success)
    {
      std::cout << "SUCCESS (0x" << std::hex << std::setw(8) << std::setfill('0')
                << addr << std::dec << ")" << std::endl;
    }
    else
    {
      std::cout << "FAILED" << std::endl;
    }
  }
}

void demoFormatIPv4()
{
  printSectionHeader("Converting Addresses to Strings");

  std::uint32_t addresses[] = {
    0x7F000001,  // 127.0.0.1
    0xC0A80101,  // 192.168.1.1
    0x08080808,  // 8.8.8.8
    0xFFFFFFFF,  // 255.255.255.255
    0x00000000   // 0.0.0.0
  };

  char buffer[16];

  for (std::uint32_t addr : addresses)
  {
    std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
              << addr << std::dec << " -> ";

    if (SocketConstants::formatIPv4(addr, buffer, sizeof(buffer)))
    {
      std::cout << buffer << std::endl;
    }
    else
    {
      std::cout << "ERROR" << std::endl;
    }
  }
}

void demoFromString()
{
  printSectionHeader("Creating SocketAddress from String");

  const char* addresses[] = {
    "192.168.1.1",
    "10.0.0.1",
    "127.0.0.1",
    "8.8.8.8"
  };

  char buffer[16];

  for (const char* addrStr : addresses)
  {
    SocketAddress addr = SocketConstants::fromString(addrStr);
    SocketConstants::formatIPv4(addr.ipv4.hostOrderAddress, buffer, sizeof(buffer));

    std::cout << std::left << std::setw(20) << addrStr
              << " -> " << buffer << std::endl;
  }
}

void demoRealWorldUsage()
{
  printSectionHeader("Real-World Usage Example");

  std::cout << "\nCreating a UDP socket and binding to all interfaces...\n" << std::endl;

  auto factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::cerr << "ERROR: Socket factory not initialized!" << std::endl;
    return;
  }

  auto socket = factory->createUDPSocket(SocketConfig{});
  if (!socket)
  {
    std::cerr << "ERROR: Failed to create socket!" << std::endl;
    return;
  }

  // Bind to any interface on port 0 (OS chooses port)
  SocketAddress bindAddr = SocketConstants::any();
  SocketError bindResult = socket->bind(bindAddr, SocketConstants::PORT_ANY);

  if (bindResult != SocketError::None)
  {
    std::cerr << "ERROR: Failed to bind socket!" << std::endl;
    return;
  }

  std::uint16_t localPort = socket->localPort();

  char buffer[16];
  SocketConstants::formatIPv4(bindAddr.ipv4.hostOrderAddress, buffer, sizeof(buffer));

  std::cout << "✓ Socket created successfully" << std::endl;
  std::cout << "✓ Bound to address: " << buffer << std::endl;
  std::cout << "✓ Bound to port:    " << localPort << std::endl;

  // Demonstrate sending to localhost
  std::cout << "\nPreparing to send to localhost:" << localPort << "..." << std::endl;

  SocketAddress destAddr = SocketConstants::loopback();
  SocketConstants::formatIPv4(destAddr.ipv4.hostOrderAddress, buffer, sizeof(buffer));

  std::cout << "✓ Destination: " << buffer << ":" << localPort << std::endl;

  const char* message = "Hello from SocketConstants demo!";
  SocketResult sendResult = socket->sendTo(message, strlen(message), destAddr, localPort);

  if (sendResult.succeeded())
  {
    std::cout << "✓ Sent " << sendResult.bytes << " bytes" << std::endl;
  }
  else
  {
    std::cerr << "✗ Send failed with error: " << static_cast<int>(sendResult.error) << std::endl;
  }

  std::cout << "\nSocket demo completed successfully!" << std::endl;
}

void demoComparison()
{
  printSectionHeader("Before vs After SocketConstants");

  std::cout << "\n--- BEFORE (Platform-Specific) ---\n" << std::endl;
  std::cout << "#if defined(_WIN32)" << std::endl;
  std::cout << "#include <winsock2.h>" << std::endl;
  std::cout << "#else" << std::endl;
  std::cout << "#include <netinet/in.h>" << std::endl;
  std::cout << "#endif" << std::endl;
  std::cout << "\nSocketAddress addr = SocketAddress::fromIPv4(INADDR_ANY);" << std::endl;
  std::cout << "SocketAddress dest = SocketAddress::fromIPv4(htonl(INADDR_LOOPBACK));" << std::endl;

  std::cout << "\n--- AFTER (Cross-Platform) ---\n" << std::endl;
  std::cout << "#include \"socket_constants.hpp\"" << std::endl;
  std::cout << "\nSocketAddress addr = SocketConstants::any();" << std::endl;
  std::cout << "SocketAddress dest = SocketConstants::loopback();" << std::endl;

  std::cout << "\n✓ Cleaner code" << std::endl;
  std::cout << "✓ No platform-specific #ifdef blocks" << std::endl;
  std::cout << "✓ No manual byte order conversions" << std::endl;
  std::cout << "✓ Same code works on Windows, Linux, and macOS" << std::endl;
}

int main()
{
  std::cout << "\n";
  std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║                                                            ║" << std::endl;
  std::cout << "║         SocketWire SocketConstants Demo                   ║" << std::endl;
  std::cout << "║         Cross-Platform Network Constants                  ║" << std::endl;
  std::cout << "║                                                            ║" << std::endl;
  std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;

  // Initialize sockets for real-world demo
  socketwire::initialize_sockets();

  // Run all demonstrations
  demoConstants();
  demoFactoryMethods();
  demoFromOctets();
  demoParseIPv4();
  demoFormatIPv4();
  demoFromString();
  demoRealWorldUsage();
  demoComparison();

  printSectionHeader("Demo Complete");
  std::cout << "\nAll features demonstrated successfully!" << std::endl;
  std::cout << "\nFor more information, see:" << std::endl;
  std::cout << "  - docs/SocketConstants.md" << std::endl;
  std::cout << "  - docs/MigrationToSocketConstants.md" << std::endl;
  std::cout << std::endl;

  return 0;
}
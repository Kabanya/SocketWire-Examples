#include <cstdio>
#include <iostream>
#include <thread>
#include <memory>

#include "i_socket.hpp"
#include "socket_init.hpp"
#include "socket_constants.hpp"
#include "math-duel-client.hpp"
#include "bit_stream.hpp"

using namespace socketwire; //NOLINT

std::unique_ptr<ISocket> clientSocket;
SocketAddress serverAddr;

class ClientHandler : public ISocketEventHandler
{
public:
  void onDataReceived([[maybe_unused]]const SocketAddress& from, [[maybe_unused]]std::uint16_t fromPort, 
                      const void* data, std::size_t bytesRead) override
  {
    if (bytesRead == 0)
      return;

    BitStream stream(reinterpret_cast<const uint8_t*>(data), bytesRead);
    std::string message;
    stream.read(message);
    std::cout << "\r" << message << "\n>";
    std::cout.flush();
  }
  void onSocketError(SocketError errorCode) override
  {
    std::cerr << "Socket error: " << static_cast<int>(errorCode) << std::endl;
  }
};

void client_receive_loop(ClientHandler& handler)
{
  while (true)
  {
    if (clientSocket)
    {
      clientSocket->poll(&handler);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
/* without using Socket Class :*/
// void receive_messages(int sfd)
// {
//   constexpr size_t buf_size = 1000;
//   char buffer[buf_size];

//   while (true)
//   {
//     fd_set readSet;
//     FD_ZERO(&readSet);
//     FD_SET(sfd, &readSet);

//     timeval timeout = { 0, 100000 }; // 100 ms
//     select(sfd + 1, &readSet, NULL, NULL, &timeout);

//     if (FD_ISSET(sfd, &readSet))
//     {
//       memset(buffer, 0, buf_size);
//       sockaddr_in sender_addr;
//       socklen_t addr_len = sizeof(sender_addr);

//       ssize_t bytes_received = recvfrom(sfd, buffer, buf_size - 1, 0,
//                                         (struct sockaddr*)&sender_addr, &addr_len);

//       if (bytes_received > 0)
//       {
//         std::cout << "\r" << buffer << "\n>";
//         std::cout.flush();
//       }
//     }
//   }
// }

void display_help()
{
  std::cout << "\n-----------------Client-INFO-------------------\n"
            << "/c [msg] - Send a message to all connected clients\n"
            << "/mathduel - Challenge someone to a math duel\n"
            << "Any other message will be sent to the server only\n"
            << "----------------Message-ownership----------------\n"
            << "SERVER: [msg] - Send a message to the server\n"
            << "CHAT (sender): [msg] - message from another user\n"
            << "-------------------------------------------------\n\n"
            << std::endl;
}


int main()
{
  // Initialize socket factory
  socketwire::initialize_sockets();

  ClientHandler handler;

  // Create socket factory and socket
  auto factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("Socket factory not initialized\n");
    return 1;
  }

  clientSocket = factory->createUDPSocket(SocketConfig{});
  if (!clientSocket)
  {
    printf("Cannot create socket\n");
    return 1;
  }

  SocketAddress localAddr = SocketConstants::any();
  if (clientSocket->bind(localAddr, 0) != SocketError::None)
  {
    printf("Cannot bind socket\n");
    return 1;
  }

  serverAddr = SocketConstants::loopback();
  std::uint16_t serverPort = 2025;

  std::cout << "Client is using port: " << clientSocket->localPort() << std::endl;

  BitStream connectStream;
  connectStream.write(std::string("New client!"));
  auto result = clientSocket->sendTo(connectStream.getData(), connectStream.getSizeBytes(), serverAddr, serverPort);
  if (result.failed())
  {
    printf("Failed to send connect message\n");
    return 1;
  }

  std::thread receiveThread([&handler]() { client_receive_loop(handler); });
  receiveThread.detach();

  while (true)
  {
    std::string input;
    printf(">");
    std::getline(std::cin, input);

    if (input == "/help" || input == "/h" || input == "/?" || input == "--help")
    {
      display_help();
      continue;
    }

    if (!input.empty())
    {
      BitStream stream;
      stream.write(input);
      clientSocket->sendTo(stream.getData(), stream.getSizeBytes(), serverAddr, serverPort);
    }
  }
  return 0;
}

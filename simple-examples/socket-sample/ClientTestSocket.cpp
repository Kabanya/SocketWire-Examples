#if defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <iostream>
#include <thread>

#include "net_socket.hpp"
#include "ClientTestSocket.h"
#include "bit_stream.hpp"

using namespace socketwire; //NOLINT

Socket clientSocket;
sockaddr_in serverAddr;


class ClientHandler : public EventHandler
{
public:
  void onDataReceived(const RecvData& recv_data) override
  {
    if (recv_data.bytesRead == 0)
      return;

    BitStream stream(reinterpret_cast<const uint8_t*>(recv_data.data), recv_data.bytesRead);
    std::string message;
    stream.read(message);
    std::cout << "\r" << message << "\n>";
    std::cout.flush();
  }
  void onSocketError(int) override {}
};

void client_receive_loop()
{
  while (true)
  {
    clientSocket.pollReceive();
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
  ClientHandler handler;
  clientSocket.setEventHandler(&handler);

  if (clientSocket.bind(nullptr, "0") != 0)
  {
    printf("Cannot bind socket\n");
    return 1;
  }

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  serverAddr.sin_port = htons(2025);

  std::cout << "Client is using port: " << clientSocket.getLocalPort() << std::endl;

  BitStream connectStream;
  connectStream.write(std::string("New client!"));
  clientSocket.sendTo(connectStream.getData(), connectStream.getSizeBytes(), serverAddr);

  std::thread receiveThread([]() { client_receive_loop(); });
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
      clientSocket.sendTo(stream.getData(), stream.getSizeBytes(), serverAddr);
    }
  }
  return 0;
}

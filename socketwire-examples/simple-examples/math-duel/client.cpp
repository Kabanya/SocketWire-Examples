#include "client.hpp"

#include <cstdio>
#include <iostream>
#include <memory>
#include <print>
#include <thread>

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

std::unique_ptr<ISocket> client_socket;
SocketAddress server_addr;

class ClientHandler : public ISocketEventHandler {
 public:
  void OnDataReceived([[maybe_unused]] const SocketAddress& from,
                      [[maybe_unused]] std::uint16_t from_port,
                      const void* data, std::size_t bytes_read) override {
    if (bytes_read == 0) return;

    BitStream stream(reinterpret_cast<const uint8_t*>(data), bytes_read);
    std::string message;
    stream.Read(message);
    std::cout << "\r" << message << "\n>";
    std::cout.flush();
  }
  void OnSocketError(SocketError error_code) override {
    std::cerr << "Socket error: " << static_cast<int>(error_code) << '\n';
  }
};

void ClientReceiveLoop(ClientHandler& handler) {
  while (true) {
    if (client_socket) {
      client_socket->Poll(&handler);
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
//                                         (struct sockaddr*)&sender_addr,
//                                         &addr_len);

//       if (bytes_received > 0)
//       {
//         std::cout << "\r" << buffer << "\n>";
//         std::cout.flush();
//       }
//     }
//   }
// }

void DisplayHelp() {
  std::cout << "\n-----------------Client-INFO-------------------\n"
            << "/c [msg] - Send a message to all connected clients\n"
            << "/mathduel - Challenge someone to a math duel\n"
            << "Any other message will be sent to the server only\n"
            << "----------------Message-ownership----------------\n"
            << "SERVER: [msg] - Send a message to the server\n"
            << "CHAT (sender): [msg] - message from another user\n"
            << "-------------------------------------------------\n\n"
            << '\n';
}

int main(int argc, const char** argv) {
  const std::uint16_t server_port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_MATH_DUEL_PORT", 2025);

  // Initialize socket factory
  socketwire::InitializeSockets();

  ClientHandler handler;

  // Create socket factory and socket
  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Socket factory not initialized");
    return 1;
  }

  client_socket = factory->CreateUdpSocket(SocketConfig{});
  if (!client_socket) {
    std::println("Cannot create socket");
    return 1;
  }

  SocketAddress const local_addr = SocketConstants::Any();
  if (client_socket->Bind(local_addr, 0) != SocketError::kNone) {
    std::println("Cannot bind socket");
    return 1;
  }

  server_addr = SocketConstants::Loopback();

  std::cout << "Client is using port: " << client_socket->LocalPort() << '\n';

  BitStream connect_stream;
  connect_stream.Write(std::string("New client!"));
  auto result = client_socket->SendTo(connect_stream.GetData(),
                                      connect_stream.GetSizeBytes(),
                                      server_addr, server_port);
  if (result.Failed()) {
    std::println("Failed to send connect message");
    return 1;
  }

  std::thread receive_thread([&handler]() { ClientReceiveLoop(handler); });
  receive_thread.detach();

  while (true) {
    std::string input;
    std::print(">");
    if (!std::getline(std::cin, input)) break;

    if (input == "/help" || input == "/h" || input == "/?" ||
        input == "--help") {
      DisplayHelp();
      continue;
    }

    if (!input.empty()) {
      BitStream stream;
      stream.Write(input);
      client_socket->SendTo(stream.GetData(), stream.GetSizeBytes(),
                            server_addr, server_port);
    }
  }
  return 0;
}

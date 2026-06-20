#include "server.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <print>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include "bit_stream.hpp"
#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

std::unique_ptr<ISocket> server_socket;
std::vector<Client> clients;
std::queue<Client> duel_queue;
std::vector<MathDuel> active_duels;

std::string ClientToString(const Client& client) {
  // Convert IPv4 address from host order to dotted decimal
  uint32_t const host_addr = client.addr.ipv4.hostOrderAddress;
  char buf[16];  // INET_ADDRSTRLEN
  socket_constants::FormatIPv4(host_addr, buf, sizeof(buf));
  return std::string(buf) + ":" + std::to_string(client.port);
}

void MsgToAllClients(const std::vector<Client>& clients,
                     const std::string& message) {
  BitStream stream;
  stream.Write(message);

  for (const Client& client : clients) {
    if (server_socket) {
      server_socket->SendTo(stream.GetData(), stream.GetSizeBytes(),
                            client.addr, client.port);
    }
  }
  std::println("msg to all clients: {}", message);
}

void MsgToClient(const Client& client, const std::string& message) {
  BitStream stream;
  stream.Write(message);
  if (server_socket) {
    server_socket->SendTo(stream.GetData(), stream.GetSizeBytes(), client.addr,
                          client.port);
  }
  std::println("msg to client: {}", message);
}

MathProblem GenerateMathProblem() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(1, 30);

  int const num1 = dis(gen);
  int const num2 = dis(gen);
  int const num3 = dis(gen);

  std::uniform_int_distribution<> op_dis(0, 2);
  int const op1 = op_dis(gen);
  int const op2 = op_dis(gen);

  int result = 0;
  std::string problem;

  if (op1 == 0) {
    problem = std::to_string(num1) + " + " + std::to_string(num2);
    result = num1 + num2;
  } else if (op1 == 1) {
    problem = std::to_string(num1) + " - " + std::to_string(num2);
    result = num1 - num2;
  } else {
    problem = std::to_string(num1) + " * " + std::to_string(num2);
    result = num1 * num2;
  }

  if (op2 == 0) {
    problem += " + " + std::to_string(num3);
    result += num3;
  } else if (op2 == 1) {
    problem += " - " + std::to_string(num3);
    result -= num3;
  } else {
    problem = "(" + problem + ") * " + std::to_string(num3);
    result *= num3;
  }

  problem += " = ?";

  MathProblem math_problem;
  math_problem.problem = problem;
  math_problem.answer = result;

  return math_problem;
}

void StartMathDuel(const Client& challenger, const Client& opponent,
                   std::vector<Client>& all_clients) {
  MathProblem const math_problem = GenerateMathProblem();

  MathDuel duel;
  duel.challenger = challenger;
  duel.opponent = opponent;
  duel.answer = math_problem.answer;
  duel.problem = math_problem.problem;
  duel.active = true;
  duel.solved = false;

  active_duels.push_back(duel);

  std::string const announcement =
    "MATH DUEL STARTING: " + ClientToString(challenger) + " vs " +
    ClientToString(opponent);
  MsgToAllClients(all_clients, announcement);

  std::string const challenge = "MATH DUEL PROBLEM: " + math_problem.problem +
                                "\nAnswer with /ans <your answer>";
  MsgToClient(challenger, challenge);
  MsgToClient(opponent, challenge);

  std::println("Math duel started between {} and {}, answer: {}",
               ClientToString(challenger), ClientToString(opponent),
               math_problem.answer);
}

bool IsInDuel(const Client& client, MathDuel** current_duel) {
  for (auto& duel : active_duels) {
    if (!duel.active || duel.solved) continue;

    if ((client.addr.ipv4.hostOrderAddress ==
           duel.challenger.addr.ipv4.hostOrderAddress &&
         client.port == duel.challenger.port) ||
        (client.addr.ipv4.hostOrderAddress ==
           duel.opponent.addr.ipv4.hostOrderAddress &&
         client.port == duel.opponent.port)) {
      if (current_duel != nullptr) *current_duel = &duel;
      return true;
    }
  }
  return false;
}

void Mathduel(const std::string& message, const Client& current_client,
              std::vector<Client>& clients) {
  if (message == "/mathduel") {
    if (IsInDuel(current_client)) {
      MsgToClient(current_client, "You are already in a math duel!");
      return;
    }

    if (duel_queue.empty()) {
      duel_queue.push(current_client);
      MsgToClient(current_client, "Waiting for an opponent...");
      MsgToAllClients(clients, "CHAT (Server): " + current_client.id +
                                 " wants math duel! Type /mathduel to join.");
    } else {
      Client const opponent = duel_queue.front();
      duel_queue.pop();

      bool opponent_exists = false;
      for (const Client& client : clients) {
        if (client.addr.ipv4.hostOrderAddress ==
              opponent.addr.ipv4.hostOrderAddress &&
            client.port == opponent.port &&
            (client.addr.ipv4.hostOrderAddress !=
               current_client.addr.ipv4.hostOrderAddress ||
             client.port != current_client.port)) {
          opponent_exists = true;
          break;
        }
      }

      if (opponent_exists) {
        StartMathDuel(current_client, opponent, clients);
      } else {
        duel_queue.push(current_client);
        MsgToClient(current_client, "Waiting for an opponent...");
        MsgToAllClients(clients, "CHAT (Server): " + current_client.id +
                                   " wants math duel! Type /mathduel to join.");
      }
    }
  }

  if (message.length() > 5 && message.starts_with("/ans ")) {
    std::string const ans_str = message.substr(5);
    int user_answer = 0;
    bool valid_input = true;

    try {
      user_answer = std::stoi(ans_str);
    } catch (const std::exception& e) {
      MsgToClient(current_client, "Invalid answer format. Use /ans <number>");
      valid_input = false;
    }

    if (valid_input) {
      MathDuel* current_duel = nullptr;
      if (!IsInDuel(current_client, &current_duel) ||
          (current_duel == nullptr)) {
        MsgToClient(current_client, "You are not in an active math duel!");
      } else {
        if (user_answer == current_duel->answer) {
          std::string const winner_id = ClientToString(current_client);
          std::string const announcement =
            "MATH DUEL RESULT: " + winner_id + " won duel with true answer: " +
            std::to_string(current_duel->answer) + "!";
          MsgToAllClients(clients, announcement);

          current_duel->solved = true;
          current_duel->active = false;
        } else {
          MsgToClient(current_client, "Wrong answer! Try again.");
        }
      }
    }
  }
}

void HandlePacket(const SocketAddress& from, std::uint16_t from_port,
                  const void* data, std::size_t bytes_read) {
  if (bytes_read == 0) return;

  BitStream stream(reinterpret_cast<const uint8_t*>(data), bytes_read);
  std::string message;
  stream.Read(message);

  Client current_client;
  current_client.addr = from;
  current_client.port = from_port;
  current_client.id = ClientToString(current_client);

  bool client_exists = false;
  for (const Client& client : clients) {
    if (client.addr.ipv4.hostOrderAddress == from.ipv4.hostOrderAddress &&
        client.port == from_port) {
      client_exists = true;
      current_client = client;
      break;
    }
  }

  if (!client_exists) {
    clients.push_back(current_client);
    std::string const welcome_msg =
      "\n/c - message to all users\n/mathduel - challenge someone to a "
      "math duel\n/help - for help";
    MsgToClient(current_client, welcome_msg);
  }

  Mathduel(message, current_client, clients);

  if (message.length() > 3 && message.starts_with("/c ")) {
    std::string const chat_message = message.substr(3);
    std::string const sender_info = ClientToString(current_client);

    std::println("msg from ({}): {}", sender_info, chat_message);
    std::string const broadcast_msg =
      "CHAT (" + sender_info + "): " + chat_message;
    MsgToAllClients(clients, broadcast_msg);
  } else if (message != "/mathduel" &&
             (message.length() <= 5 || !message.starts_with("/ans "))) {
    std::println("({}) {}", current_client.id, message);
  }
}

void DrainServerSocket() {
  while (server_socket) {
    std::uint8_t buffer[4096]{};
    SocketAddress from{};
    std::uint16_t from_port = 0;
    const auto result =
      server_socket->Receive(buffer, sizeof(buffer), from, from_port);
    if (result.error == SocketError::kWouldBlock) return;
    if (result.Failed()) {
      std::cerr << "Socket error: " << ToString(result.error) << '\n';
      return;
    }
    if (result.bytes <= 0) return;

    HandlePacket(from, from_port, buffer,
                 static_cast<std::size_t>(result.bytes));
  }
}

int main(int argc, const char** argv) {
  // Initialize socket factory
  socketwire::InitializeSockets();

  const std::uint16_t port = socketwire_examples::PortFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_MATH_DUEL_PORT", 2025);

  // Create socket factory and socket
  auto factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Socket factory not initialized");
    return 1;
  }

  server_socket = factory->CreateUdpSocket(SocketConfig{});
  if (!server_socket) {
    std::println("Cannot create socket");
    return 1;
  }

  SocketAddress const bind_addr = socket_constants::Any();
  if (server_socket->Bind(bind_addr, port) != SocketError::kNone) {
    std::println("cannot bind socket");
    return 1;
  }
  std::println("listening on port {}!", static_cast<unsigned>(port));

  while (true) {
    DrainServerSocket();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}

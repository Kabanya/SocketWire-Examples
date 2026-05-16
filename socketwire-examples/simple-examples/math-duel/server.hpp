#ifndef SERVER_TEST_SOCKET_H
#define SERVER_TEST_SOCKET_H

#include <cstdint>
#include <queue>
#include <string>

#include "i_socket.hpp"

using namespace socketwire;  // NOLINT

struct Client {
  SocketAddress addr;
  std::uint16_t port{};
  std::string id;
};

struct MathProblem {
  std::string problem;
  int answer{};
};

struct MathDuel {
  Client challenger;
  Client opponent;
  std::string problem;
  bool active{};
  bool solved{};
  int answer{};
};

extern std::queue<Client> duel_queue;
extern std::vector<MathDuel> activeDuels;

// Msg & input logic
std::string client_to_string(const Client& client);
void MsgToAllClients(const std::vector<Client>& clients,
                     const std::string& message);
void MsgToClient(const Client& client, const std::string& message);

// MATH logic
MathProblem GenerateMathProblem();
void StartMathDuel(const Client& challenger, const Client& opponent,
                   std::vector<Client>& all_clients);
bool IsInDuel(const Client& client, MathDuel** current_duel = nullptr);
void Mathduel(const std::string& message, const Client& current_client,
              std::vector<Client>& clients);

#endif  // SERVER_TEST_SOCKET_H

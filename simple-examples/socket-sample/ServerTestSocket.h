#ifndef SERVER_TEST_SOCKET_H
#define SERVER_TEST_SOCKET_H

#include "i_socket.hpp"
#include <string>
#include <queue>
#include <cstdint>

using namespace socketwire; //NOLINT

struct Client
{
  SocketAddress addr;
  std::uint16_t port;
  std::string id;
};

struct MathProblem
{
  std::string problem;
  int answer;
};

struct MathDuel
{
  Client challenger;
  Client opponent;
  std::string problem;
  bool active;
  bool solved;
  int answer;
};

extern std::queue<Client> duelQueue;
extern std::vector<MathDuel> activeDuels;

// Msg & input logic
std::string client_to_string(const Client& client);
void msg_to_all_clients(const std::vector<Client>& clients, const std::string& message);
void msg_to_client(const Client& client, const std::string& message);

// MATH logic
MathProblem generate_math_problem();
void start_math_duel(
  const Client& challenger, const Client& opponent, std::vector<Client>& all_clients);
bool is_in_duel(const Client& client, MathDuel** current_duel = nullptr);
void mathduel(const std::string& message, const Client& current_client, std::vector<Client>& clients);

#endif // SERVER_TEST_SOCKET_H

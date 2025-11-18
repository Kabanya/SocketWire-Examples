#include "bit_stream.hpp"
#if defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <random>
#include <memory>

#include "ServerTestSocket.h"
#include "i_socket.hpp"

using namespace socketwire; //NOLINT

namespace socketwire {
extern void register_posix_socket_factory();
}

std::unique_ptr<ISocket> serverSocket;
std::vector<Client> clients;
std::queue<Client> duelQueue;
std::vector<MathDuel> activeDuels;

std::string client_to_string(const Client& client)
{
  // Convert IPv4 address from host order to dotted decimal
  uint32_t hostAddr = client.addr.ipv4.hostOrderAddress;
  in_addr addr;
  addr.s_addr = htonl(hostAddr);
  return std::string(inet_ntoa(addr)) + ":" + std::to_string(client.port);
}

void msg_to_all_clients(const std::vector<Client>& clients, const std::string& message)
{
  BitStream stream;
  stream.write(message);

  for (const Client& client : clients)
  {
    if (serverSocket)
    {
      serverSocket->sendTo(stream.getData(), stream.getSizeBytes(), client.addr, client.port);
    }
  }
  printf("msg to all clients: %s\n", message.c_str());
}

void msg_to_client(const Client& client, const std::string& message)
{
  BitStream stream;
  stream.write(message);
  if (serverSocket)
  {
    serverSocket->sendTo(stream.getData(), stream.getSizeBytes(), client.addr, client.port);
  }
  printf("msg to client: %s\n", message.c_str());
}

MathProblem generate_math_problem()
{
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(1, 30);

  int num1 = dis(gen);
  int num2 = dis(gen);
  int num3 = dis(gen);

  std::uniform_int_distribution<> opDis(0, 2);
  int op1 = opDis(gen);
  int op2 = opDis(gen);

  int result = 0;
  std::string problem;

  if (op1 == 0)
  {
    problem = std::to_string(num1) + " + " + std::to_string(num2);
    result = num1 + num2;
  }
  else if (op1 == 1)
  {
    problem = std::to_string(num1) + " - " + std::to_string(num2);
    result = num1 - num2;
  }
  else
  {
    problem = std::to_string(num1) + " * " + std::to_string(num2);
    result = num1 * num2;
  }

  if (op2 == 0)
  {
    problem += " + " + std::to_string(num3);
    result += num3;
  }
  else if (op2 == 1)
  {
    problem += " - " + std::to_string(num3);
    result -= num3;
  }
  else
  {
    problem = "(" + problem + ") * " + std::to_string(num3);
    result *= num3;
  }

  problem += " = ?";

  MathProblem mathProblem;
  mathProblem.problem = problem;
  mathProblem.answer = result;

  return mathProblem;
}

void start_math_duel(
  const Client& challenger, const Client& opponent, std::vector<Client>& all_clients)
{
  MathProblem mathProblem = generate_math_problem();

  MathDuel duel;
  duel.challenger = challenger;
  duel.opponent = opponent;
  duel.answer = mathProblem.answer;
  duel.problem = mathProblem.problem;
  duel.active = true;
  duel.solved = false;

  activeDuels.push_back(duel);

  std::string announcement =
    "MATH DUEL STARTING: " + client_to_string(challenger) + " vs " + client_to_string(opponent);
  msg_to_all_clients(all_clients, announcement);

  std::string challenge =
    "MATH DUEL PROBLEM: " + mathProblem.problem + "\nAnswer with /ans <your answer>";
  msg_to_client(challenger, challenge);
  msg_to_client(opponent, challenge);

  printf(
    "Math duel started between %s and %s, answer: %d\n",
    client_to_string(challenger).c_str(),
    client_to_string(opponent).c_str(),
    mathProblem.answer);
}

bool is_in_duel(const Client& client, MathDuel** current_duel)
{
  for (size_t i = 0; i < activeDuels.size(); i++)
  {
    MathDuel& duel = activeDuels[i];
    if (!duel.active || duel.solved)
      continue;

    if (
      (client.addr.ipv4.hostOrderAddress == duel.challenger.addr.ipv4.hostOrderAddress &&
       client.port == duel.challenger.port) ||
      (client.addr.ipv4.hostOrderAddress == duel.opponent.addr.ipv4.hostOrderAddress &&
       client.port == duel.opponent.port))
    {
      if (current_duel != nullptr)
        *current_duel = &duel;
      return true;
    }
  }
  return false;
}

void mathduel(const std::string& message, const Client& current_client, std::vector<Client>& clients)
{
  if (message == "/mathduel")
  {
    if (is_in_duel(current_client))
    {
      msg_to_client(current_client, "You are already in a math duel!");
      return;
    }

    if (duelQueue.empty())
    {
      duelQueue.push(current_client);
      msg_to_client(current_client, "Waiting for an opponent...");
      msg_to_all_clients(
        clients,
        "CHAT (Server): " + current_client.id + " wants math duel! Type /mathduel to join.");
    }
    else
    {
      Client opponent = duelQueue.front();
      duelQueue.pop();

      bool opponentExists = false;
      for (const Client& client : clients)
      {
        if (
          client.addr.ipv4.hostOrderAddress == opponent.addr.ipv4.hostOrderAddress &&
          client.port == opponent.port &&
          (client.addr.ipv4.hostOrderAddress != current_client.addr.ipv4.hostOrderAddress ||
            client.port != current_client.port))
        {
          opponentExists = true;
          break;
        }
      }

      if (opponentExists)
      {
        start_math_duel(current_client, opponent, clients);
      }
      else
      {
        duelQueue.push(current_client);
        msg_to_client(current_client, "Waiting for an opponent...");
        msg_to_all_clients(
          clients,
          "CHAT (Server): " + current_client.id + " wants math duel! Type /mathduel to join.");
      }
    }
  }

  if (message.length() > 5 && message.substr(0, 5) == "/ans ")
  {
    std::string ansStr = message.substr(5);
    int userAnswer;
    bool validInput = true;

    try
    {
      userAnswer = std::stoi(ansStr);
    }
    catch (const std::exception& e)
    {
      msg_to_client(current_client, "Invalid answer format. Use /ans <number>");
      validInput = false;
    }

    if (validInput)
    {
      MathDuel* currentDuel = nullptr;
      if (!is_in_duel(current_client, &currentDuel) || (currentDuel == nullptr))
      {
        msg_to_client(current_client, "You are not in an active math duel!");
      }
      else
      {
        if (userAnswer == currentDuel->answer)
        {
          std::string winnerId = client_to_string(current_client);
          std::string announcement = "MATH DUEL RESULT: " + winnerId +
            " won duel with true answer: " + std::to_string(currentDuel->answer) + "!";
          msg_to_all_clients(clients, announcement);

          currentDuel->solved = true;
          currentDuel->active = false;
        }
        else
        {
          msg_to_client(current_client, "Wrong answer! Try again.");
        }
      }
    }
  }
}

class ServerHandler : public ISocketEventHandler
{
public:
  void onDataReceived(const SocketAddress& from, std::uint16_t fromPort,
                      const void* data, std::size_t bytesRead) override
  {
    if (bytesRead == 0)
      return;

    BitStream stream(reinterpret_cast<const uint8_t*>(data), bytesRead);
    std::string message;
    stream.read(message);

    Client currentClient;
    currentClient.addr = from;
    currentClient.port = fromPort;
    currentClient.id = client_to_string(currentClient);

    bool clientExists = false;
    for (const Client& client : clients)
    {
      if (client.addr.ipv4.hostOrderAddress == from.ipv4.hostOrderAddress &&
          client.port == fromPort)
      {
        clientExists = true;
        currentClient = client;
        break;
      }
    }

    if (!clientExists)
    {
      clients.push_back(currentClient);
      std::string welcomeMsg = "\n/c - message to all users\n/mathduel - challenge someone to a "
                               "math duel\n/help - for help";
      msg_to_client(currentClient, welcomeMsg);
    }

    mathduel(message, currentClient, clients);

    if (message.length() > 3 && message.substr(0, 3) == "/c ")
    {
      std::string chatMessage = message.substr(3);
      std::string senderInfo = client_to_string(currentClient);

      printf("msg from (%s): %s\n", senderInfo.c_str(), chatMessage.c_str());
      std::string broadcastMsg = "CHAT (" + senderInfo + "): " + chatMessage;
      msg_to_all_clients(clients, broadcastMsg);
    }
    else if (message != "/mathduel" && (message.length() <= 5 || message.substr(0, 5) != "/ans "))
    {
      printf("(%s) %s\n", currentClient.id.c_str(), message.c_str());
    }
  }
  void onSocketError(SocketError errorCode) override
  {
    std::cerr << "Socket error: " << static_cast<int>(errorCode) << std::endl;
  }
};

int main()
{
  // Initialize socket factory
  socketwire::register_posix_socket_factory();

  const char* port_str = "2025";
  uint16_t port = 2025;

  ServerHandler handler;

  // Create socket factory and socket
  auto factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("Socket factory not initialized\n");
    return 1;
  }

  serverSocket = factory->createUDPSocket(SocketConfig{});
  if (!serverSocket)
  {
    printf("Cannot create socket\n");
    return 1;
  }

  SocketAddress bindAddr = SocketAddress::fromIPv4(INADDR_ANY);
  if (serverSocket->bind(bindAddr, port) != SocketError::None)
  {
    printf("cannot bind socket\n");
    return 1;
  }
  printf("listening on port %s!\n", port_str);

  while (true)
  {
    serverSocket->poll(&handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}

#include "NetSocket.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace socketwire; //NOLINT

class PrintHandler : public EventHandler
{
public:
  void onDataReceived(const RecvData& recv_data) override
  {
    std::cout << "Received: " << std::string(recv_data.data, recv_data.bytesRead) << std::endl;
  }
  void onSocketError(int) override {}
};

int main()
{
  Socket client;
  PrintHandler handler;
  client.setEventHandler(&handler);

  client.bind(nullptr, "0");

  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  dest.sin_port = htons(40404);

  std::string msg = "Hello from use case of Socket class!";
  client.sendTo(msg.c_str(), msg.size(), dest);

  for (int i = 0; i < 10; ++i)
  {
    client.pollReceive();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}

#include "net_socket.hpp"
#include <iostream>
#include <unistd.h>

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
  Socket server;
  PrintHandler handler;
  server.setEventHandler(&handler);

  if (server.bind(nullptr, "40404") != 0)
  {
    std::cout << "Bind failed\n";
    return 1;
  }
  std::cout << "Server listening on port 40404\n";
  while (true)
  {
    server.pollReceive();
    usleep(10000);
  }
}

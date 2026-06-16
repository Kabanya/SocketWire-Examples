#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "i_socket.hpp"
#include "socket_init.hpp"

using namespace socketwire;  // NOLINT

namespace {

constexpr const char* kDefaultWebSocketUrl = "ws://127.0.0.1:8765";
constexpr int kMessagesToSend = 5;
constexpr double kSendIntervalMs = 1000.0;
constexpr double kTimeoutMs = 10000.0;

EM_JS(char*, ReadWebSocketUrlFromPage, (), {
  var url = "ws://127.0.0.1:8765";
  if (typeof window != = "undefined" && window.location) {
    var params = new URLSearchParams(window.location.search);
    var configured = params.get("url");
    if (configured && configured.length > 0) {
      url = configured;
    }
  }

  var length = lengthBytesUTF8(url) + 1;
  var buffer = _malloc(length);
  stringToUTF8(url, buffer, length);
  return buffer;
});

std::string WebSocketUrlFromPage() {
  char* url = ReadWebSocketUrlFromPage();
  if (url == nullptr) return kDefaultWebSocketUrl;

  std::string result(url);
  std::free(url);
  return result.empty() ? kDefaultWebSocketUrl : result;
}

struct DemoState {
  std::unique_ptr<ISocket> socket;
  SocketAddress peer = SocketAddress::FromIPv4(0);
  double nextSendAtMs = 0.0;
  double stopAtMs = 0.0;
  int sent = 0;
  int received = 0;
};

void Finish(DemoState* state, const char* reason) {
  if (state != nullptr && state->socket != nullptr) state->socket->Close();
  std::printf("%s\n", reason);
  delete state;
  emscripten_cancel_main_loop();
}

bool DrainIncoming(DemoState* state) {
  while (true) {
    std::uint8_t buffer[4096]{};
    SocketAddress from{};
    std::uint16_t fromPort = 0;
    const SocketResult result =
      state->socket->Receive(buffer, sizeof(buffer), from, fromPort);
    if (result.error == SocketError::kWouldBlock) return true;
    if (result.error == SocketError::kClosed) {
      Finish(state, "demo stopped");
      return false;
    }
    if (result.Failed()) {
      std::printf("socket error: %s\n", ToString(result.error));
      Finish(state, "demo failed");
      return false;
    }
    if (result.bytes <= 0) return true;

    const std::string payload(reinterpret_cast<const char*>(buffer),
                              static_cast<std::size_t>(result.bytes));
    ++state->received;
    std::printf("received echo %d/%d: %s\n", state->received, kMessagesToSend,
                payload.c_str());
  }
}

void Tick(void* userData) {
  auto* state = static_cast<DemoState*>(userData);
  if (state == nullptr) {
    emscripten_cancel_main_loop();
    return;
  }

  if (!DrainIncoming(state)) return;

  const double now = emscripten_get_now();

  if (state->sent < kMessagesToSend && now >= state->nextSendAtMs) {
    const int sequence = state->sent + 1;
    const std::string payload =
      "SocketWire Emscripten WebSocket ping #" + std::to_string(sequence);
    const SocketResult result =
      state->socket->SendTo(payload.data(), payload.size(), state->peer, 0);

    if (result.Failed()) {
      std::printf("send failed: %s\n", ToString(result.error));
      Finish(state, "demo failed");
      return;
    }

    ++state->sent;
    state->nextSendAtMs = now + kSendIntervalMs;
    std::printf("sent %d/%d\n", state->sent, kMessagesToSend);
  }

  if (state->received >= kMessagesToSend) {
    Finish(state, "demo finished");
    return;
  }

  if (now >= state->stopAtMs) {
    Finish(state, "demo timed out");
  }
}

}  // namespace

int main() {
  InitializeSockets();

  WebSocketConfig config;
  config.url = WebSocketUrlFromPage();
  std::printf("connecting to %s\n", config.url.c_str());

  auto socket = CreateEmscriptenWebSocketClient(config);
  if (socket == nullptr) {
    std::printf("Cannot create Emscripten WebSocket client\n");
    return 1;
  }

  auto* state = new DemoState;
  state->socket = std::move(socket);
  state->nextSendAtMs = emscripten_get_now() + 250.0;
  state->stopAtMs = emscripten_get_now() + kTimeoutMs;

  emscripten_set_main_loop_arg(Tick, state, 0, true);
  return 0;
}

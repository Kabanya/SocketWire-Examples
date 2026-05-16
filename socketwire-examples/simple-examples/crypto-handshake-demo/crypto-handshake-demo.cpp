#include <cstdio>
#include <cstring>
#include <print>
#include <string>

#include "crypto.hpp"

int main() {
  const auto init = socketwire::crypto::Initialize();
  if (!init.ok) {
    std::println("crypto is unavailable: {}",
                 socketwire::crypto::ToString(init.error));
    return 0;
  }

#if SOCKETWIRE_HAVE_LIBSODIUM
  if (!socketwire::crypto::CipherSuiteSupported(
        socketwire::crypto::CipherSuite::kXChaCha20Poly1305)) {
    std::println("XChaCha20-Poly1305 is not supported by this build");
    return 0;
  }

  const auto client_keys = socketwire::crypto::KeyPair::Generate();
  const auto server_keys = socketwire::crypto::KeyPair::Generate();
  if (!client_keys.Valid() || !server_keys.Valid()) {
    std::println("key generation failed");
    return 1;
  }

  socketwire::crypto::HandshakeState client;
  socketwire::crypto::HandshakeState server;
  client.StartClient(client_keys);
  server.StartServer(server_keys);

  socketwire::BitStream client_hello;
  auto result = client.WriteClientHello(client_hello);
  if (!result.ok || !server.ProcessClientHello(client_hello.GetData(),
                                               client_hello.GetSizeBytes())) {
    std::println("client hello failed");
    return 1;
  }

  socketwire::BitStream server_hello;
  result = server.WriteServerHello(server_hello);
  if (!result.ok || !client.ProcessServerHello(server_hello.GetData(),
                                               server_hello.GetSizeBytes())) {
    std::println("server hello failed");
    return 1;
  }

  if (!client.Completed() || !server.Completed()) {
    std::println("handshake did not complete");
    return 1;
  }

  auto client_crypto = client.CreateClientCryptoContext();
  auto server_crypto = server.CreateServerCryptoContext();
  if (!client_crypto.IsReady() || !server_crypto.IsReady()) {
    std::println("crypto contexts are not ready");
    return 1;
  }

  const std::string plain = "secure-ping";
  socketwire::BitStream encrypted;
  if (!client_crypto.Encrypt(
        7, reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
        encrypted)) {
    std::println("encrypt failed");
    return 1;
  }

  socketwire::BitStream decrypted;
  if (!server_crypto.Decrypt(7, encrypted.GetData(), encrypted.GetSizeBytes(),
                             decrypted)) {
    std::println("decrypt failed");
    return 1;
  }

  const std::string roundTrip(
    reinterpret_cast<const char*>(decrypted.GetData()),
    decrypted.GetSizeBytes());
  std::println("handshake completed; encrypted {} bytes; decrypted='{}'",
               encrypted.GetSizeBytes(), roundTrip);
  return roundTrip == plain ? 0 : 1;
#else
  std::printf(
    "libsodium headers were not available when SocketWire was built\n");
  return 0;
#endif
}

#include <cstdio>
#include <cstring>
#include <string>

#include "crypto.hpp"

int main() {
  const auto init = socketwire::crypto::Initialize();
  if (!init.ok) {
    std::printf("crypto is unavailable: %s\n",
                socketwire::crypto::ToString(init.error));
    return 0;
  }

#if SOCKETWIRE_HAVE_LIBSODIUM
  if (!socketwire::crypto::CipherSuiteSupported(
        socketwire::crypto::CipherSuite::kXChaCha20Poly1305)) {
    std::printf("XChaCha20-Poly1305 is not supported by this build\n");
    return 0;
  }

  const auto clientKeys = socketwire::crypto::KeyPair::Generate();
  const auto serverKeys = socketwire::crypto::KeyPair::Generate();
  if (!clientKeys.Valid() || !serverKeys.Valid()) {
    std::printf("key generation failed\n");
    return 1;
  }

  socketwire::crypto::HandshakeState client;
  socketwire::crypto::HandshakeState server;
  client.StartClient(clientKeys);
  server.StartServer(serverKeys);

  socketwire::BitStream clientHello;
  auto result = client.WriteClientHello(clientHello);
  if (!result.ok || !server.ProcessClientHello(clientHello.GetData(),
                                               clientHello.GetSizeBytes())) {
    std::printf("client hello failed\n");
    return 1;
  }

  socketwire::BitStream serverHello;
  result = server.WriteServerHello(serverHello);
  if (!result.ok || !client.ProcessServerHello(serverHello.GetData(),
                                               serverHello.GetSizeBytes())) {
    std::printf("server hello failed\n");
    return 1;
  }

  if (!client.Completed() || !server.Completed()) {
    std::printf("handshake did not complete\n");
    return 1;
  }

  auto clientCrypto = client.CreateClientCryptoContext();
  auto serverCrypto = server.CreateServerCryptoContext();
  if (!clientCrypto.IsReady() || !serverCrypto.IsReady()) {
    std::printf("crypto contexts are not ready\n");
    return 1;
  }

  const std::string plain = "secure-ping";
  socketwire::BitStream encrypted;
  if (!clientCrypto.Encrypt(
        7, reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
        encrypted)) {
    std::printf("encrypt failed\n");
    return 1;
  }

  socketwire::BitStream decrypted;
  if (!serverCrypto.Decrypt(7, encrypted.GetData(), encrypted.GetSizeBytes(),
                            decrypted)) {
    std::printf("decrypt failed\n");
    return 1;
  }

  const std::string roundTrip(
    reinterpret_cast<const char*>(decrypted.GetData()),
    decrypted.GetSizeBytes());
  std::printf("handshake completed; encrypted %zu bytes; decrypted='%s'\n",
              encrypted.GetSizeBytes(), roundTrip.c_str());
  return roundTrip == plain ? 0 : 1;
#else
  std::printf(
    "libsodium headers were not available when SocketWire was built\n");
  return 0;
#endif
}

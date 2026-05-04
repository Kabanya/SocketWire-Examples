#include "crypto.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main()
{
  const auto init = socketwire::crypto::initialize();
  if (!init.ok)
  {
    std::printf("crypto is unavailable: %s\n", socketwire::crypto::to_string(init.error));
    return 0;
  }

#if SOCKETWIRE_HAVE_LIBSODIUM
  if (!socketwire::crypto::cipherSuiteSupported(socketwire::crypto::CipherSuite::XChaCha20Poly1305))
  {
    std::printf("XChaCha20-Poly1305 is not supported by this build\n");
    return 0;
  }

  const auto clientKeys = socketwire::crypto::KeyPair::generate();
  const auto serverKeys = socketwire::crypto::KeyPair::generate();
  if (!clientKeys.valid() || !serverKeys.valid())
  {
    std::printf("key generation failed\n");
    return 1;
  }

  socketwire::crypto::HandshakeState client;
  socketwire::crypto::HandshakeState server;
  client.startClient(clientKeys);
  server.startServer(serverKeys);

  socketwire::BitStream clientHello;
  auto result = client.writeClientHello(clientHello);
  if (!result.ok || !server.processClientHello(clientHello.getData(), clientHello.getSizeBytes()))
  {
    std::printf("client hello failed\n");
    return 1;
  }

  socketwire::BitStream serverHello;
  result = server.writeServerHello(serverHello);
  if (!result.ok || !client.processServerHello(serverHello.getData(), serverHello.getSizeBytes()))
  {
    std::printf("server hello failed\n");
    return 1;
  }

  if (!client.completed() || !server.completed())
  {
    std::printf("handshake did not complete\n");
    return 1;
  }

  auto clientCrypto = client.createClientCryptoContext();
  auto serverCrypto = server.createServerCryptoContext();
  if (!clientCrypto.isReady() || !serverCrypto.isReady())
  {
    std::printf("crypto contexts are not ready\n");
    return 1;
  }

  const std::string plain = "secure-ping";
  socketwire::BitStream encrypted;
  if (!clientCrypto.encrypt(7,
                            reinterpret_cast<const unsigned char*>(plain.data()),
                            plain.size(),
                            encrypted))
  {
    std::printf("encrypt failed\n");
    return 1;
  }

  socketwire::BitStream decrypted;
  if (!serverCrypto.decrypt(7, encrypted.getData(), encrypted.getSizeBytes(), decrypted))
  {
    std::printf("decrypt failed\n");
    return 1;
  }

  const std::string roundTrip(reinterpret_cast<const char*>(decrypted.getData()), decrypted.getSizeBytes());
  std::printf("handshake completed; encrypted %zu bytes; decrypted='%s'\n",
              encrypted.getSizeBytes(),
              roundTrip.c_str());
  return roundTrip == plain ? 0 : 1;
#else
  std::printf("libsodium headers were not available when SocketWire was built\n");
  return 0;
#endif
}

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <print>
#include <string>

#include "crypto.hpp"

namespace {

std::string ShortHex(const socketwire::crypto::PublicKey& key) {
  std::ostringstream out;
  for (std::size_t i = 0; i < 6; ++i) {
    out << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(key[i]);
  }
  return out.str() + "...";
}

bool Check(socketwire::crypto::Result result, const char* step) {
  if (result.ok) return true;
  std::println("{} failed: {}", step, socketwire::crypto::ToString(result.error));
  return false;
}

}  // namespace

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

  std::println("SocketWire crypto handshake demo");
  std::println("client public key: {}", ShortHex(client_keys.publicKey));
  std::println("server public key: {}", ShortHex(server_keys.publicKey));

  socketwire::crypto::HandshakeState client;
  socketwire::crypto::HandshakeState server;
  if (!Check(client.StartClient(client_keys, server_keys.publicKey),
             "start client")) {
    return 1;
  }
  if (!Check(server.StartServer(server_keys), "start server")) return 1;

  socketwire::BitStream client_hello;
  auto result = client.WriteClientHello(client_hello);
  if (!Check(result, "write client hello") ||
      !Check(server.ProcessClientHello(client_hello.GetData(),
                                       client_hello.GetSizeBytes()),
             "process client hello")) {
    return 1;
  }
  std::println("client -> server: ClientHello ({} bytes)",
               client_hello.GetSizeBytes());

  const auto wrong_server_keys = socketwire::crypto::KeyPair::Generate();
  socketwire::crypto::HandshakeState client_with_wrong_pin;
  if (!Check(client_with_wrong_pin.StartClient(client_keys,
                                               wrong_server_keys.publicKey),
             "start pinned client")) {
    return 1;
  }

  socketwire::BitStream server_hello;
  result = server.WriteServerHello(server_hello);
  if (!Check(result, "write server hello")) return 1;
  std::println("server -> client: ServerHello ({} bytes)",
               server_hello.GetSizeBytes());

  const auto rejected = client_with_wrong_pin.ProcessServerHello(
    server_hello.GetData(), server_hello.GetSizeBytes());
  if (rejected.ok ||
      rejected.error != socketwire::crypto::CryptoError::kInvalidPeerKey) {
    std::println("wrong server-key check failed");
    return 1;
  }
  std::println("wrong pinned server key rejected: {}",
               socketwire::crypto::ToString(rejected.error));

  if (!Check(client.ProcessServerHello(server_hello.GetData(),
                                       server_hello.GetSizeBytes()),
             "process server hello")) {
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
  constexpr std::uint64_t sequence = 7;
  if (!Check(client_crypto.Encrypt(
               sequence,
               reinterpret_cast<const unsigned char*>(plain.data()),
               plain.size(), encrypted),
             "encrypt")) {
    return 1;
  }
  std::println("encrypted seq={} plaintext='{}' -> {} bytes (nonce + MAC)",
               sequence, plain, encrypted.GetSizeBytes());

  socketwire::BitStream decrypted;
  const auto wrong_seq =
    server_crypto.Decrypt(sequence + 1, encrypted.GetData(),
                          encrypted.GetSizeBytes(), decrypted);
  if (wrong_seq.ok ||
      wrong_seq.error != socketwire::crypto::CryptoError::kDecryptFailed) {
    std::println("wrong sequence check failed");
    return 1;
  }
  std::println("wrong sequence rejected: {}",
               socketwire::crypto::ToString(wrong_seq.error));

  if (!Check(server_crypto.Decrypt(sequence, encrypted.GetData(),
                                   encrypted.GetSizeBytes(), decrypted),
             "decrypt")) {
    return 1;
  }

  const std::string round_trip(
    reinterpret_cast<const char*>(decrypted.GetData()),
    decrypted.GetSizeBytes());
  const auto replay =
    server_crypto.Decrypt(sequence, encrypted.GetData(),
                          encrypted.GetSizeBytes(), decrypted);
  if (replay.ok ||
      replay.error != socketwire::crypto::CryptoError::kReplayDetected) {
    std::println("replay check failed");
    return 1;
  }
  std::println("replay rejected: {}", socketwire::crypto::ToString(replay.error));

  std::println("decrypted='{}'", round_trip);
  std::println("crypto-handshake-demo self-check passed");
  return round_trip == plain ? 0 : 1;
#else
  std::printf(
    "libsodium headers were not available when SocketWire was built\n");
  return 0;
#endif
}

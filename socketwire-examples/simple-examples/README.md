# SocketWire Simple Examples

Small focused examples for learning one SocketWire feature at a time.

## Examples

| Directory | Targets | Demonstrates |
| --- | --- | --- |
| `header-demos` | `bitstream-demo`, `constants-demo` | Basic `BitStream` usage and socket/address constants. |
| `address-demo` | `address-demo` | IPv4/IPv6 parsing, formatting, `tryFromString`, `loopbackIPv6`, and a local IPv6 probe. |
| `safe-bitstream-demo` | `safe-bitstream-demo` | `BitStream::try_read*` with `std::expected` and malformed packet handling. |
| `echo` | `echo-server`, `echo-client` | Minimal UDP socket send/receive. |
| `math-duel` | `math-duel-server`, `math-duel-client` | Console multiplayer flow ported from `MIPT-networked/w1`. |
| `packet-stream` | `packet-stream-server`, `packet-stream-client` | Reliable connection packet stream ported from `MIPT-networked/w3`. |
| `channels-demo` | `channels-demo-server`, `channels-demo-client` | Reliable commands on channel 0, unreliable movement and unsequenced snapshots on channel 1. |
| `large-message-demo` | `large-message-demo-server`, `large-message-demo-client` | Reliable fragmentation/reassembly for payloads larger than `maxPacketSize`. |
| `stats-window-demo` | `stats-window-demo-server`, `stats-window-demo-client` | Send window limits, RTT, in-flight count, and packet counters. |
| `crypto-handshake-demo` | `crypto-handshake-demo` | Minimal crypto handshake and AEAD round trip, with graceful no-libsodium fallback. |

## Run Commands

Single-process demos:

```sh
./build/bin/bitstream-demo
./build/bin/constants-demo
./build/bin/address-demo
./build/bin/safe-bitstream-demo
./build/bin/crypto-handshake-demo
```

Client/server demos should be run from two terminals. Start the server first:

```sh
./build/bin/echo-server
./build/bin/echo-client

./build/bin/packet-stream-server
./build/bin/packet-stream-client

./build/bin/channels-demo-server
./build/bin/channels-demo-client

./build/bin/large-message-demo-server
./build/bin/large-message-demo-client

./build/bin/stats-window-demo-server
./build/bin/stats-window-demo-client
```

## Ports

| Example | Port |
| --- | --- |
| `echo` | `40404` |
| `packet-stream` | `53473` |
| `channels-demo` | `53474` |
| `large-message-demo` | `53475` |
| `stats-window-demo` | `53476` |

All new client/server demos use loopback by default.

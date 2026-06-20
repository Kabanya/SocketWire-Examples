# Raylib Examples

Interactive SocketWire examples built with Raylib.


## Examples
| Example | Source port | What it demonstrates | Run |
| --- | --- | --- | --- |
| `entity-eater` | `MIPT-networked/w4` | Server-authoritative entity eating game with score, timer, and game-over events | `entity-eater-server`, `entity-eater-client` |
| `lobby-dots` | `MIPT-networked/w2` | Lobby, game-server discovery, player position sync, ping updates | `lobby-dots-lobby`, `lobby-dots-game-server`, `lobby-dots-client` |
| `prediction-ships` | `MIPT-networked/w5` | Fixed-timestep snapshots, client prediction, reconciliation | `prediction-ships-server`, `prediction-ships-client` |
| `ship-swarm` | `MIPT-networked/w7` | Quantized input/snapshots and bandwidth display with many ships | `ship-swarm-server`, `ship-swarm-client` |
| `cipher-ships` | `MIPT-networked/w10` | Per-client key exchange, XOR payload cipher, fuzzed input packets | `cipher-ships-server`, `cipher-ships-client` |
| `projectile-arena` | standalone | Server-authoritative arena game with reliable fire events and unreliable movement | `projectile-arena-server`, `projectile-arena-client` |
| `persistent-arena` | standalone | Dedicated server with a persistent in-memory world and clients that enter a server address | `persistent-arena-server`, `persistent-arena-client` |

## Run Commands

Client/server demos should be run from two terminals. Start the server first:

```sh
./build/bin/entity-eater-server
./build/bin/entity-eater-client

./build/bin/prediction-ships-server
./build/bin/prediction-ships-client

./build/bin/ship-swarm-server
./build/bin/ship-swarm-client

# LAN / custom endpoint:
./build/bin/ship-swarm-server 10133
./build/bin/ship-swarm-client --host 192.168.1.50 --port 10133
./build/bin/ship-swarm-client --host my-machine.local --port 10133

./build/bin/cipher-ships-server
./build/bin/cipher-ships-client

./build/bin/projectile-arena-server
./build/bin/projectile-arena-client

./build/bin/persistent-arena-server
./build/bin/persistent-arena-client
```

`lobby-dots` uses three processes. Start the lobby and game server first:

```sh
./build/bin/lobby-dots-lobby
./build/bin/lobby-dots-game-server
./build/bin/lobby-dots-client
```

## Ports

| Example | Port |
| --- | --- |
| `entity-eater` | `10131` |
| `lobby-dots` lobby | `10887` |
| `lobby-dots` game server | `10888` |
| `prediction-ships` | `10131` |
| `ship-swarm` | `10133` |
| `cipher-ships` | `10131` |
| `projectile-arena` | `53477` |
| `persistent-arena` | `53478` |

Most Raylib client/server demos use loopback by default. `persistent-arena` starts its client on a connection screen so another machine can enter the server host and port.

## Run Notes
- Most examples use loopback by default; start the server first, then the client.
- Clients that accept `--host` resolve IPv4 addresses, `localhost`, and DNS/mDNS names.
- `ship-swarm` uses UDP port `10133` by default. The client accepts `--host` and `--port`, or positional `host port`, for LAN runs.
- `lobby-dots` uses lobby port `10887` and game-server port `10888`.
- In `lobby-dots`, start `lobby-dots-lobby` and `lobby-dots-game-server`, then open `lobby-dots-client`; press Enter in the client window to start the game session.
- For a LAN `persistent-arena` run, start the server on the machine that owns the world:

```sh
cmake -S . -B build
cmake --build build --target persistent-arena-server
./build/bin/persistent-arena-server 53478
```

Find the server address on Linux with `ip addr`. If `ufw` is enabled, allow the UDP port with `sudo ufw allow 53478/udp`.

On each client machine:

```sh
cmake -S . -B build
cmake --build build --target persistent-arena-client
./build/bin/persistent-arena-client
```

Enter the server address, for example `192.168.1.50` or `my-machine.local`, and port `53478` in the client window.

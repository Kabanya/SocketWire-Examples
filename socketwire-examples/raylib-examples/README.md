# Raylib Examples

Interactive SocketWire examples built with Raylib.


## Examples
| Directory | Targets | Sources | Demonstrates |
| --- | --- | --- | --- |
| `entity-eater` | `entity-eater-server`, `entity-eater-client` | [server.cpp](entity-eater/server.cpp), [main.cpp](entity-eater/main.cpp), [protocol.cpp](entity-eater/protocol.cpp), [protocol.h](entity-eater/protocol.h), [entity.h](entity-eater/entity.h) | Server-authoritative entity eating game with score, timer, and game-over events ported from `MIPT-networked/w4`. |
| `lobby-dots` | `lobby-dots-lobby`, `lobby-dots-game-server`, `lobby-dots-client` | [lobby.cpp](lobby-dots/lobby.cpp), [game_server.cpp](lobby-dots/game_server.cpp), [client.cpp](lobby-dots/client.cpp) | Lobby, game-server discovery, player position sync, and ping updates ported from `MIPT-networked/w2`. |
| `prediction-ships` | `prediction-ships-server`, `prediction-ships-client` | [server.cpp](prediction-ships/server.cpp), [main.cpp](prediction-ships/main.cpp), [protocol.cpp](prediction-ships/protocol.cpp), [protocol.h](prediction-ships/protocol.h), [entity.cpp](prediction-ships/entity.cpp), [entity.h](prediction-ships/entity.h), [mathUtils.h](prediction-ships/mathUtils.h) | Fixed-timestep snapshots, client prediction, and reconciliation ported from `MIPT-networked/w5`. |
| `ship-swarm` | `ship-swarm-server`, `ship-swarm-client` | [server.cpp](ship-swarm/server.cpp), [main.cpp](ship-swarm/main.cpp), [protocol.cpp](ship-swarm/protocol.cpp), [protocol.h](ship-swarm/protocol.h), [entity.cpp](ship-swarm/entity.cpp), [entity.h](ship-swarm/entity.h), [mathUtils.h](ship-swarm/mathUtils.h), [quantisation.h](ship-swarm/quantisation.h) | Quantized input/snapshots and bandwidth display with many ships ported from `MIPT-networked/w7`. |
| `projectile-arena` | `projectile-arena-server`, `projectile-arena-client` | [server.cpp](projectile-arena/server.cpp), [client.cpp](projectile-arena/client.cpp), [protocol.hpp](projectile-arena/protocol.hpp) | Server-authoritative arena game with reliable fire events and unreliable movement. |

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

./build/bin/projectile-arena-server
./build/bin/projectile-arena-client
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
| `projectile-arena` | `53477` |

Most Raylib client/server demos use loopback by default.

## Run Notes
- Most examples use loopback by default; start the server first, then the client.
- Clients that accept `--host` resolve IPv4/IPv6, `localhost`, and DNS/mDNS names in OS order.
- Servers created through the shared helper try dual-stack bind first and fall back to IPv4.
- `ship-swarm` uses UDP port `10133` by default. The client accepts `--host` and `--port`, or positional `host port`, for LAN runs.
- `lobby-dots` uses lobby port `10887` and game-server port `10888`.
- In `lobby-dots`, start `lobby-dots-lobby` and `lobby-dots-game-server`, then open `lobby-dots-client`; press Enter in the client window to start the game session.

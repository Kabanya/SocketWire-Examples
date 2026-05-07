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

## Run Commands

Client/server demos should be run from two terminals. Start the server first:

```sh
./build/bin/entity-eater-server
./build/bin/entity-eater-client

./build/bin/prediction-ships-server
./build/bin/prediction-ships-client

./build/bin/ship-swarm-server
./build/bin/ship-swarm-client

./build/bin/cipher-ships-server
./build/bin/cipher-ships-client

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
| `ship-swarm` | `10131` |
| `cipher-ships` | `10131` |
| `projectile-arena` | `53477` |

All Raylib client/server demos use loopback by default.

## Run Notes
- Most examples use loopback and port `10131`; start the server first, then the client.
- `lobby-dots` uses lobby port `10887` and game-server port `10888`.
- In `lobby-dots`, start `lobby-dots-lobby` and `lobby-dots-game-server`, then open `lobby-dots-client`; press Enter in the client window to start the game session.

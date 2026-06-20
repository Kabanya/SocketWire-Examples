# SocketWire Examples

Collection of example projects that demonstrate how to use [SocketWire](https://github.com/Kabanya/SocketWire).

## Contents
- `socketwire-examples/simple-examples/` — minimal servers/clients, bitstream and echo examples.
- `socketwire-examples/raylib-examples/` — games and interactive examples using Raylib.

## Example Guides
- [Simple examples](socketwire-examples/simple-examples/README.md)
- [Raylib examples](socketwire-examples/raylib-examples/README.md)

## Build
```sh
cmake -S . -B build
cmake --build build
```

When `../SocketWire` exists next to this repository, CMake uses that local
checkout so examples can build against in-development SocketWire APIs.

## Prerequisites
- C++ compiler with C++23 support
- CMake 3.28 or newer
- Optional: Raylib for raylib-based examples

## License
This repository follows the [MIT Licence](LICENCE) at the project root.

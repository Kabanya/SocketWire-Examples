#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import struct


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def handshake_response(key: str) -> bytes:
    accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest())
    return (
        b"HTTP/1.1 101 Switching Protocols\r\n"
        b"Upgrade: websocket\r\n"
        b"Connection: Upgrade\r\n"
        b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n"
    )


async def read_handshake(reader: asyncio.StreamReader) -> str:
    request = await reader.readuntil(b"\r\n\r\n")
    headers: dict[str, str] = {}
    for raw_line in request.decode("latin1").split("\r\n")[1:]:
        if ":" not in raw_line:
            continue
        name, value = raw_line.split(":", 1)
        headers[name.strip().lower()] = value.strip()

    key = headers.get("sec-websocket-key")
    if not key:
        raise ValueError("missing Sec-WebSocket-Key")
    return key


async def read_frame(reader: asyncio.StreamReader) -> tuple[int, bytes]:
    first, second = await reader.readexactly(2)
    opcode = first & 0x0F
    masked = (second & 0x80) != 0
    length = second & 0x7F

    if length == 126:
        (length,) = struct.unpack("!H", await reader.readexactly(2))
    elif length == 127:
        (length,) = struct.unpack("!Q", await reader.readexactly(8))

    mask = await reader.readexactly(4) if masked else b""
    payload = await reader.readexactly(length)
    if masked:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def make_frame(opcode: int, payload: bytes) -> bytes:
    header = bytes([0x80 | opcode])
    if len(payload) < 126:
        header += bytes([len(payload)])
    elif len(payload) <= 0xFFFF:
        header += bytes([126]) + struct.pack("!H", len(payload))
    else:
        header += bytes([127]) + struct.pack("!Q", len(payload))
    return header + payload


async def handle_client(
    reader: asyncio.StreamReader, writer: asyncio.StreamWriter
) -> None:
    peer = writer.get_extra_info("peername")
    try:
        key = await read_handshake(reader)
        writer.write(handshake_response(key))
        await writer.drain()
        print(f"accepted {peer}")

        while True:
            opcode, payload = await read_frame(reader)
            if opcode == 0x8:
                writer.write(make_frame(0x8, payload[:2]))
                await writer.drain()
                break
            if opcode == 0x9:
                writer.write(make_frame(0xA, payload))
                await writer.drain()
                continue
            if opcode in (0x1, 0x2):
                print(f"echo {len(payload)} bytes to {peer}")
                writer.write(make_frame(opcode, payload))
                await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, ValueError) as exc:
        print(f"closed {peer}: {exc}")
    finally:
        writer.close()
        await writer.wait_closed()


async def run(host: str, port: int) -> None:
    server = await asyncio.start_server(handle_client, host, port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
    print(f"listening on {addresses}")
    async with server:
        await server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser(description="Minimal binary WebSocket echo server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    asyncio.run(run(args.host, args.port))


if __name__ == "__main__":
    main()

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "entity.h"

namespace socketwire {
class ReliableConnection;
}

enum MessageType : std::uint8_t {
  kEClientToServerJoin = 0,
  kEServerToClientNewEntity,
  kEServerToClientSetControlledEntity,
  kEClientToServerInput,
  kEServerToClientSnapshot,
  kEServerToClientKey
};

void SendJoin(socketwire::ReliableConnection* connection);
void SendNewEntity(socketwire::ReliableConnection* connection,
                   const Entity& ent);
void SendSetControlledEntity(socketwire::ReliableConnection* connection,
                             std::uint16_t eid);
void SendCipherKey(socketwire::ReliableConnection* connection,
                   std::uint32_t key);
void SendEntityInput(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, float thr, float steer);
void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float ori);

MessageType GetPacketType(const void* data, std::size_t size);

void DeserializeNewEntity(const void* data, std::size_t size, Entity& ent);
void DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid);
void DeserializeEntityInput(const void* data, std::size_t size,
                            std::uint16_t& eid, float& thr, float& steer);
void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori);
void DeserializeAndSetKey(const void* data, std::size_t size);

std::vector<std::uint8_t> DecipherData(const void* data, std::size_t size,
                                       std::uint32_t key);

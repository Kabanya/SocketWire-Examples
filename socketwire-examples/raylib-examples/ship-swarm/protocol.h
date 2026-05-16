#pragma once

#include <cstddef>
#include <cstdint>

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
  kEServerToClientTimeMsec
};

void SendJoin(socketwire::ReliableConnection* connection);
void SendNewEntity(socketwire::ReliableConnection* connection,
                   const Entity& ent);
void SendSetControlledEntity(socketwire::ReliableConnection* connection,
                             std::uint16_t eid);
void SendEntityInput(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, float thr, float steer);
void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float ori);
void SendTimeMsec(socketwire::ReliableConnection* connection,
                  std::uint32_t time_msec);

MessageType GetPacketType(const void* data, std::size_t size);

void DeserializeNewEntity(const void* data, std::size_t size, Entity& ent);
void DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid);
void DeserializeEntityInput(const void* data, std::size_t size,
                            std::uint16_t& eid, float& thr, float& steer);
void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori);
void DeserializeTimeMsec(const void* data, std::size_t size,
                         std::uint32_t& time_msec);

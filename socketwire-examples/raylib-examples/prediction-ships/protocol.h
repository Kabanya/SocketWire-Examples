#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "entity.h"

namespace socketwire {
class ReliableConnection;
}

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

constexpr float kFixedDt = 1.0f / 10.0f;

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
                  float x, float y, float ori, float vx, float vy, float omega,
                  TimePoint timestamp, std::uint32_t frame_number);
void SendTimeMsec(socketwire::ReliableConnection* connection,
                  std::uint32_t time_msec);

MessageType GetPacketType(const void* data, std::size_t size);

bool DeserializeNewEntity(const void* data, std::size_t size, Entity& ent);
bool DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid);
bool DeserializeEntityInput(const void* data, std::size_t size,
                            std::uint16_t& eid, float& thr, float& steer);
bool DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori, float& vx, float& vy,
                         float& omega, TimePoint& timestamp,
                         std::uint32_t& frame_number);
bool DeserializeTimeMsec(const void* data, std::size_t size,
                         std::uint32_t& time_msec);

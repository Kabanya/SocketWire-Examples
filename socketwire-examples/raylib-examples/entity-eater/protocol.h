#pragma once

#include <cstddef>
#include <cstdint>

#include "entity.h"

namespace socketwire {
class ReliableConnection;
}

enum class MessageType : std::uint8_t {
  kEClientToServerJoin = 0,
  kEServerToClientNewEntity,
  kEServerToClientSetControlledEntity,
  kEClientToServerState,
  kEServerToClientSnapshot,
  kEServerToClientEntityDevoured,
  kEServerToClientScoreUpdate,
  kEServerToClientGameTime,
  kEServerToClientGameOver
};

void SendJoin(socketwire::ReliableConnection* connection);
void SendNewEntity(socketwire::ReliableConnection* connection,
                   const Entity& ent);
void SendSetControlledEntity(socketwire::ReliableConnection* connection,
                             std::uint16_t eid);
void SendEntityState(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, float x, float y);
void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float size);

void SendEntityDevoured(socketwire::ReliableConnection* connection,
                        std::uint16_t devoured_eid, std::uint16_t devourer_eid,
                        float devourer_new_size, float devoured_new_size,
                        float new_x, float new_y);
void SendScoreUpdate(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, int score);
void SendGameOver(socketwire::ReliableConnection* connection,
                  std::uint16_t winner_eid, int winner_score);
void SendGameTime(socketwire::ReliableConnection* connection,
                  int seconds_remaining);

MessageType GetPacketType(const void* data, std::size_t size);

void DeserializeNewEntity(const void* data, std::size_t size, Entity& ent);
void DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid);
void DeserializeEntityState(const void* data, std::size_t size,
                            std::uint16_t& eid, float& x, float& y);
void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& size_out);

void DeserializeScoreUpdate(const void* data, std::size_t size,
                            std::uint16_t& eid, int& score);
void DeserializeEntityDevoured(const void* data, std::size_t size,
                               std::uint16_t& devoured_eid,
                               std::uint16_t& devourer_eid,
                               float& devourer_new_size,
                               float& devoured_new_size, float& new_x,
                               float& new_y);
void DeserializeGameOver(const void* data, std::size_t size,
                         std::uint16_t& winner_eid, int& winner_score);
void DeserializeGameTime(const void* data, std::size_t size,
                         int& seconds_remaining);

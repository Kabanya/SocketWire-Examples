#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void SendJoin(socketwire::ReliableConnection* connection) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEClientToServerJoin);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendNewEntity(socketwire::ReliableConnection* connection,
                   const Entity& ent) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientNewEntity);
  bs.Write<std::uint32_t>(ent.color);
  bs.Write<float>(ent.x);
  bs.Write<float>(ent.y);
  bs.Write<std::uint16_t>(ent.eid);
  bs.Write<bool>(ent.serverControlled);
  bs.Write<float>(ent.targetX);
  bs.Write<float>(ent.targetY);
  bs.Write<float>(ent.size);
  bs.Write<int>(ent.score);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendSetControlledEntity(socketwire::ReliableConnection* connection,
                             std::uint16_t eid) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSetControlledEntity);
  bs.Write<std::uint16_t>(eid);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendEntityState(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, float x, float y) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEClientToServerState);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  if (connection->SendUnsequenced(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float size) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSnapshot);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  bs.Write<float>(size);
  if (connection->SendUnsequenced(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendEntityDevoured(socketwire::ReliableConnection* connection,
                        std::uint16_t devoured_eid, std::uint16_t devourer_eid,
                        float new_size, float new_x, float new_y) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientEntityDevoured);
  bs.Write<std::uint16_t>(devoured_eid);
  bs.Write<std::uint16_t>(devourer_eid);
  bs.Write<float>(new_size);
  bs.Write<float>(new_x);
  bs.Write<float>(new_y);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendScoreUpdate(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, int score) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientScoreUpdate);
  bs.Write<std::uint16_t>(eid);
  bs.Write<int>(score);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendGameTime(socketwire::ReliableConnection* connection,
                  int seconds_remaining) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientGameTime);
  bs.Write<int>(seconds_remaining);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendGameOver(socketwire::ReliableConnection* connection,
                  std::uint16_t winner_eid, int winner_score) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientGameOver);
  bs.Write<std::uint16_t>(winner_eid);
  bs.Write<int>(winner_score);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

MessageType GetPacketType(const void* data, std::size_t size) {
  if (data == nullptr || size < 1) return kEClientToServerJoin;
  return static_cast<MessageType>(*static_cast<const std::uint8_t*>(data));
}

void DeserializeNewEntity(const void* data, std::size_t size, Entity& ent) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(ent.color);
  bs.Read<float>(ent.x);
  bs.Read<float>(ent.y);
  bs.Read<std::uint16_t>(ent.eid);
  bs.Read<bool>(ent.serverControlled);
  bs.Read<float>(ent.targetX);
  bs.Read<float>(ent.targetY);
  bs.Read<float>(ent.size);
  bs.Read<int>(ent.score);
}

void DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
}

void DeserializeEntityState(const void* data, std::size_t size,
                            std::uint16_t& eid, float& x, float& y) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(x);
  bs.Read<float>(y);
}

void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& size_out) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(x);
  bs.Read<float>(y);
  bs.Read<float>(size_out);
}

void DeserializeEntityDevoured(const void* data, std::size_t size,
                               std::uint16_t& devoured_eid,
                               std::uint16_t& devourer_eid, float& new_size,
                               float& new_x, float& new_y) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(devoured_eid);
  bs.Read<std::uint16_t>(devourer_eid);
  bs.Read<float>(new_size);
  bs.Read<float>(new_x);
  bs.Read<float>(new_y);
}

void DeserializeScoreUpdate(const void* data, std::size_t size,
                            std::uint16_t& eid, int& score) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<int>(score);
}

void DeserializeGameOver(const void* data, std::size_t size,
                         std::uint16_t& winner_eid, int& winner_score) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(winner_eid);
  bs.Read<int>(winner_score);
}

void DeserializeGameTime(const void* data, std::size_t size,
                         int& seconds_remaining) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<int>(seconds_remaining);
}

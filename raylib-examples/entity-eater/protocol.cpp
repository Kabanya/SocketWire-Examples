#include "protocol.h"
#include "bit_stream.hpp"

void send_join(ENetPeer *peer)
{
  BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_JOIN);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void send_new_entity(ENetPeer *peer, const Entity &ent)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);

  bs.write<uint32_t>(ent.color);
  bs.write<float>(ent.x);
  bs.write<float>(ent.y);
  bs.write<uint16_t>(ent.eid);
  bs.write<bool>(ent.serverControlled);
  bs.write<float>(ent.targetX);
  bs.write<float>(ent.targetY);
  bs.write<float>(ent.size);
  bs.write<int>(ent.score);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void send_set_controlled_entity(ENetPeer *peer, uint16_t eid)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.write<uint16_t>(eid);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void send_entity_state(ENetPeer *peer, uint16_t eid, float x, float y)
{
  BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_STATE);
  bs.write<uint16_t>(eid);

  bs.write<float>(x);
  bs.write<float>(y);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_UNSEQUENCED);
  enet_peer_send(peer, 1, packet);
}

void send_snapshot(ENetPeer *peer, uint16_t eid, float x, float y, float size)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.write<uint16_t>(eid);

  bs.write<float>(x);
  bs.write<float>(y);
  bs.write<float>(size);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_UNSEQUENCED);
  enet_peer_send(peer, 1, packet);
}

MessageType get_packet_type(ENetPacket *packet)
{
  return (MessageType)*packet->data;
}

void deserialize_new_entity(ENetPacket *packet, Entity &ent)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);

  bs.read<uint32_t>(ent.color);
  bs.read<float>(ent.x);
  bs.read<float>(ent.y);
  bs.read<uint16_t>(ent.eid);
  bs.read<bool>(ent.serverControlled);
  bs.read<float>(ent.targetX);
  bs.read<float>(ent.targetY);
  bs.read<float>(ent.size);
  bs.read<int>(ent.score);
}

void deserialize_set_controlled_entity(ENetPacket *packet, uint16_t &eid)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
}

void deserialize_entity_state(ENetPacket *packet, uint16_t &eid, float &x, float &y)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_snapshot(ENetPacket *packet, uint16_t &eid, float &x, float &y, float &size)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
  bs.read<float>(size);
}

void send_entity_devoured(ENetPeer *peer, uint16_t devoured_eid, uint16_t devourer_eid, float new_size, float new_x, float new_y)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_ENTITY_DEVOURED);
  bs.write<uint16_t>(devoured_eid);
  bs.write<uint16_t>(devourer_eid);
  bs.write<float>(new_size);
  bs.write<float>(new_x);
  bs.write<float>(new_y);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void deserialize_entity_devoured(ENetPacket *packet, uint16_t &devoured_eid, uint16_t &devourer_eid, float &new_size, float &new_x, float &new_y)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(devoured_eid);
  bs.read<uint16_t>(devourer_eid);
  bs.read<float>(new_size);
  bs.read<float>(new_x);
  bs.read<float>(new_y);
}

void send_score_update(ENetPeer *peer, uint16_t eid, int score)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SCORE_UPDATE);
  bs.write<uint16_t>(eid);
  bs.write<int>(score);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void deserialize_score_update(ENetPacket *packet, uint16_t &eid, int &score)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<int>(score);
}

void send_game_time(ENetPeer *peer, int seconds_remaining)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_GAME_TIME);
  bs.write<int>(seconds_remaining);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void send_game_over(ENetPeer *peer, uint16_t winner_eid, int winner_score)
{
  BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_GAME_OVER);
  bs.write<uint16_t>(winner_eid);
  bs.write<int>(winner_score);

  ENetPacket *packet = enet_packet_create(bs.getData(), bs.getSizeBytes(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

void deserialize_game_over(ENetPacket *packet, uint16_t &winner_eid, int &winner_score)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(winner_eid);
  bs.read<int>(winner_score);
}

void deserialize_game_time(ENetPacket *packet, int &seconds_remaining)
{
  BitStream bs(packet->data, packet->dataLength);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<int>(seconds_remaining);
}
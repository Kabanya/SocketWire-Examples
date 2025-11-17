#pragma once
#include <cstdint>
#include <cstddef>

// Forward declarations for SocketWire
namespace socketwire {
  class ReliableConnection;
  class BitStream;
}

enum MessageType : uint8_t
{
  // Client -> Server
  E_CLIENT_TO_SERVER_JOIN = 0,
  E_CLIENT_TO_SERVER_MOVE = 1,
  // Server -> Client
  E_SERVER_TO_CLIENT_WELCOME = 2,
  E_SERVER_TO_CLIENT_PLAYER_JOINED = 3,
  E_SERVER_TO_CLIENT_PLAYER_LEFT = 4,
  E_SERVER_TO_CLIENT_PLAYER_UPDATE = 5,
  E_SERVER_TO_CLIENT_COIN_SPAWN = 6,
  E_SERVER_TO_CLIENT_COIN_COLLECTED = 7,
  E_SERVER_TO_CLIENT_SCORE_UPDATE = 8
};

// Helper functions
MessageType get_packet_type(const void* data, size_t size);

// Client -> Server
void send_join(socketwire::ReliableConnection* connection);
void send_player_move(socketwire::ReliableConnection* connection, float x, float y);

// Server -> Client
void send_welcome(socketwire::ReliableConnection* connection, uint16_t player_id);
void send_player_joined(socketwire::ReliableConnection* connection, uint16_t player_id, float x, float y, uint32_t color);
void send_player_left(socketwire::ReliableConnection* connection, uint16_t player_id);
void send_player_update(socketwire::ReliableConnection* connection, uint16_t player_id, float x, float y);
void send_coin_spawn(socketwire::ReliableConnection* connection, uint16_t coin_id, float x, float y);
void send_coin_collected(socketwire::ReliableConnection* connection, uint16_t coin_id, uint16_t player_id);
void send_score_update(socketwire::ReliableConnection* connection, uint16_t player_id, int score);

// Deserialization
void deserialize_welcome(const void* data, size_t size, uint16_t& player_id);
void deserialize_player_joined(const void* data, size_t size, uint16_t& player_id, float& x, float& y, uint32_t& color);
void deserialize_player_left(const void* data, size_t size, uint16_t& player_id);
void deserialize_player_update(const void* data, size_t size, uint16_t& player_id, float& x, float& y);
void deserialize_player_move(const void* data, size_t size, float& x, float& y);
void deserialize_coin_spawn(const void* data, size_t size, uint16_t& coin_id, float& x, float& y);
void deserialize_coin_collected(const void* data, size_t size, uint16_t& coin_id, uint16_t& player_id);
void deserialize_score_update(const void* data, size_t size, uint16_t& player_id, int& score);
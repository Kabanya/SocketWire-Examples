#pragma once

#include <cstdint>
#include <vector>

#include "bit_stream.hpp"

namespace projectile_arena {

constexpr std::uint16_t kKPort = 53477;

enum class MessageType : std::uint8_t {
  kJoin = 1,
  kWelcome = 2,
  kInput = 3,
  kFire = 4,
  kSnapshot = 5,
};

struct InputState {
  std::uint32_t tick = 0;
  float axisX = 0.0f;
  float axisY = 0.0f;
};

struct FireCommand {
  std::uint32_t tick = 0;
  float aimX = 0.0f;
  float aimY = 0.0f;
};

struct PlayerSnapshot {
  std::uint16_t id = 0;
  float x = 0.0f;
  float y = 0.0f;
};

struct ProjectileSnapshot {
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
};

struct WorldSnapshot {
  std::uint32_t tick = 0;
  std::vector<PlayerSnapshot> players;
  std::vector<ProjectileSnapshot> projectiles;
};

inline socketwire::BitStream MakeJoin() {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kJoin));
  return stream;
}

inline socketwire::BitStream MakeWelcome(std::uint16_t player_id) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kWelcome));
  stream.Write<std::uint16_t>(player_id);
  return stream;
}

inline socketwire::BitStream MakeInput(const InputState& input) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kInput));
  stream.Write<std::uint32_t>(input.tick);
  stream.Write<float>(input.axisX);
  stream.Write<float>(input.axisY);
  return stream;
}

inline socketwire::BitStream MakeFire(const FireCommand& fire) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kFire));
  stream.Write<std::uint32_t>(fire.tick);
  stream.Write<float>(fire.aimX);
  stream.Write<float>(fire.aimY);
  return stream;
}

inline socketwire::BitStream MakeSnapshot(const WorldSnapshot& snapshot) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kSnapshot));
  stream.Write<std::uint32_t>(snapshot.tick);
  stream.Write<std::uint16_t>(
    static_cast<std::uint16_t>(snapshot.players.size()));
  for (const auto& player : snapshot.players) {
    stream.Write<std::uint16_t>(player.id);
    stream.Write<float>(player.x);
    stream.Write<float>(player.y);
  }

  stream.Write<std::uint16_t>(
    static_cast<std::uint16_t>(snapshot.projectiles.size()));
  for (const auto& projectile : snapshot.projectiles) {
    stream.Write<std::uint16_t>(projectile.id);
    stream.Write<std::uint16_t>(projectile.ownerId);
    stream.Write<float>(projectile.x);
    stream.Write<float>(projectile.y);
  }
  return stream;
}

inline bool ReadType(socketwire::BitStream& stream, MessageType& type) {
  const auto value = stream.TryRead<std::uint8_t>();
  if (!value) return false;
  type = static_cast<MessageType>(*value);
  return true;
}

inline bool ReadWelcome(socketwire::BitStream& stream,
                        std::uint16_t& player_id) {
  const auto value = stream.TryRead<std::uint16_t>();
  if (!value) return false;
  player_id = *value;
  return true;
}

inline bool ReadInput(socketwire::BitStream& stream, InputState& input) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto axis_x = stream.TryRead<float>();
  const auto axis_y = stream.TryRead<float>();
  if (!tick || !axis_x || !axis_y) return false;
  input = InputState{*tick, *axis_x, *axis_y};
  return true;
}

inline bool ReadFire(socketwire::BitStream& stream, FireCommand& fire) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto aim_x = stream.TryRead<float>();
  const auto aim_y = stream.TryRead<float>();
  if (!tick || !aim_x || !aim_y) return false;
  fire = FireCommand{*tick, *aim_x, *aim_y};
  return true;
}

inline bool ReadSnapshot(socketwire::BitStream& stream,
                         WorldSnapshot& snapshot) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto player_count = stream.TryRead<std::uint16_t>();
  if (!tick || !player_count) return false;

  snapshot = {};
  snapshot.tick = *tick;
  snapshot.players.reserve(*player_count);
  for (std::uint16_t i = 0; i < *player_count; ++i) {
    const auto id = stream.TryRead<std::uint16_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    if (!id || !x || !y) return false;
    snapshot.players.push_back(PlayerSnapshot{*id, *x, *y});
  }

  const auto projectile_count = stream.TryRead<std::uint16_t>();
  if (!projectile_count) return false;

  snapshot.projectiles.reserve(*projectile_count);
  for (std::uint16_t i = 0; i < *projectile_count; ++i) {
    const auto id = stream.TryRead<std::uint16_t>();
    const auto owner_id = stream.TryRead<std::uint16_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    if (!id || !owner_id || !x || !y) return false;
    snapshot.projectiles.push_back(ProjectileSnapshot{*id, *owner_id, *x, *y});
  }
  return true;
}

}  // namespace projectile_arena

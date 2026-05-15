#pragma once

#include <cstdint>
#include <vector>

#include "bit_stream.hpp"

namespace persistent_arena {

constexpr std::uint16_t K_PORT = 53478;
constexpr float K_WORLD_WIDTH = 900.0f;
constexpr float K_WORLD_HEIGHT = 600.0f;

enum class MessageType : std::uint8_t {
  Join = 1,
  Welcome = 2,
  Input = 3,
  Fire = 4,
  Snapshot = 5,
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
  std::uint32_t score = 0;
};

struct ProjectileSnapshot {
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
};

struct ResourceSnapshot {
  std::uint16_t id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float radius = 0.0f;
  std::uint16_t value = 0;
};

struct WorldSnapshot {
  std::uint32_t tick = 0;
  std::uint32_t globalScore = 0;
  std::vector<PlayerSnapshot> players;
  std::vector<ProjectileSnapshot> projectiles;
  std::vector<ResourceSnapshot> resources;
};

inline socketwire::BitStream make_join() {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Join));
  return stream;
}

inline socketwire::BitStream make_welcome(std::uint16_t playerId) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Welcome));
  stream.Write<std::uint16_t>(playerId);
  return stream;
}

inline socketwire::BitStream make_input(const InputState& input) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Input));
  stream.Write<std::uint32_t>(input.tick);
  stream.Write<float>(input.axisX);
  stream.Write<float>(input.axisY);
  return stream;
}

inline socketwire::BitStream make_fire(const FireCommand& fire) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Fire));
  stream.Write<std::uint32_t>(fire.tick);
  stream.Write<float>(fire.aimX);
  stream.Write<float>(fire.aimY);
  return stream;
}

inline socketwire::BitStream make_snapshot(const WorldSnapshot& snapshot) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Snapshot));
  stream.Write<std::uint32_t>(snapshot.tick);
  stream.Write<std::uint32_t>(snapshot.globalScore);

  stream.Write<std::uint16_t>(
    static_cast<std::uint16_t>(snapshot.players.size()));
  for (const auto& player : snapshot.players) {
    stream.Write<std::uint16_t>(player.id);
    stream.Write<float>(player.x);
    stream.Write<float>(player.y);
    stream.Write<std::uint32_t>(player.score);
  }

  stream.Write<std::uint16_t>(
    static_cast<std::uint16_t>(snapshot.projectiles.size()));
  for (const auto& projectile : snapshot.projectiles) {
    stream.Write<std::uint16_t>(projectile.id);
    stream.Write<std::uint16_t>(projectile.ownerId);
    stream.Write<float>(projectile.x);
    stream.Write<float>(projectile.y);
  }

  stream.Write<std::uint16_t>(
    static_cast<std::uint16_t>(snapshot.resources.size()));
  for (const auto& resource : snapshot.resources) {
    stream.Write<std::uint16_t>(resource.id);
    stream.Write<float>(resource.x);
    stream.Write<float>(resource.y);
    stream.Write<float>(resource.radius);
    stream.Write<std::uint16_t>(resource.value);
  }
  return stream;
}

inline bool read_type(socketwire::BitStream& stream, MessageType& type) {
  const auto value = stream.TryRead<std::uint8_t>();
  if (!value) return false;
  type = static_cast<MessageType>(*value);
  return true;
}

inline bool read_welcome(socketwire::BitStream& stream,
                         std::uint16_t& playerId) {
  const auto value = stream.TryRead<std::uint16_t>();
  if (!value) return false;
  playerId = *value;
  return true;
}

inline bool read_input(socketwire::BitStream& stream, InputState& input) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto axisX = stream.TryRead<float>();
  const auto axisY = stream.TryRead<float>();
  if (!tick || !axisX || !axisY) return false;
  input = InputState{*tick, *axisX, *axisY};
  return true;
}

inline bool read_fire(socketwire::BitStream& stream, FireCommand& fire) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto aimX = stream.TryRead<float>();
  const auto aimY = stream.TryRead<float>();
  if (!tick || !aimX || !aimY) return false;
  fire = FireCommand{*tick, *aimX, *aimY};
  return true;
}

inline bool read_snapshot(socketwire::BitStream& stream,
                          WorldSnapshot& snapshot) {
  const auto tick = stream.TryRead<std::uint32_t>();
  const auto globalScore = stream.TryRead<std::uint32_t>();
  const auto playerCount = stream.TryRead<std::uint16_t>();
  if (!tick || !globalScore || !playerCount) return false;

  snapshot = {};
  snapshot.tick = *tick;
  snapshot.globalScore = *globalScore;
  snapshot.players.reserve(*playerCount);
  for (std::uint16_t i = 0; i < *playerCount; ++i) {
    const auto id = stream.TryRead<std::uint16_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    const auto score = stream.TryRead<std::uint32_t>();
    if (!id || !x || !y || !score) return false;
    snapshot.players.push_back(PlayerSnapshot{*id, *x, *y, *score});
  }

  const auto projectileCount = stream.TryRead<std::uint16_t>();
  if (!projectileCount) return false;

  snapshot.projectiles.reserve(*projectileCount);
  for (std::uint16_t i = 0; i < *projectileCount; ++i) {
    const auto id = stream.TryRead<std::uint16_t>();
    const auto ownerId = stream.TryRead<std::uint16_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    if (!id || !ownerId || !x || !y) return false;
    snapshot.projectiles.push_back(ProjectileSnapshot{*id, *ownerId, *x, *y});
  }

  const auto resourceCount = stream.TryRead<std::uint16_t>();
  if (!resourceCount) return false;

  snapshot.resources.reserve(*resourceCount);
  for (std::uint16_t i = 0; i < *resourceCount; ++i) {
    const auto id = stream.TryRead<std::uint16_t>();
    const auto x = stream.TryRead<float>();
    const auto y = stream.TryRead<float>();
    const auto radius = stream.TryRead<float>();
    const auto value = stream.TryRead<std::uint16_t>();
    if (!id || !x || !y || !radius || !value) return false;
    snapshot.resources.push_back(
      ResourceSnapshot{*id, *x, *y, *radius, *value});
  }
  return true;
}

}  // namespace persistent_arena

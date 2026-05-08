#pragma once

#include "bit_stream.hpp"

#include <cstdint>
#include <vector>

namespace projectile_arena
{

constexpr std::uint16_t K_PORT = 53477;

enum class MessageType : std::uint8_t
{
  Join = 1,
  Welcome = 2,
  Input = 3,
  Fire = 4,
  Snapshot = 5,
};

struct InputState
{
  std::uint32_t tick = 0;
  float axisX = 0.0f;
  float axisY = 0.0f;
};

struct FireCommand
{
  std::uint32_t tick = 0;
  float aimX = 0.0f;
  float aimY = 0.0f;
};

struct PlayerSnapshot
{
  std::uint16_t id = 0;
  float x = 0.0f;
  float y = 0.0f;
};

struct ProjectileSnapshot
{
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
};

struct WorldSnapshot
{
  std::uint32_t tick = 0;
  std::vector<PlayerSnapshot> players;
  std::vector<ProjectileSnapshot> projectiles;
};

inline socketwire::BitStream make_join()
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Join));
  return stream;
}

inline socketwire::BitStream make_welcome(std::uint16_t playerId)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Welcome));
  stream.write<std::uint16_t>(playerId);
  return stream;
}

inline socketwire::BitStream make_input(const InputState& input)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Input));
  stream.write<std::uint32_t>(input.tick);
  stream.write<float>(input.axisX);
  stream.write<float>(input.axisY);
  return stream;
}

inline socketwire::BitStream make_fire(const FireCommand& fire)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Fire));
  stream.write<std::uint32_t>(fire.tick);
  stream.write<float>(fire.aimX);
  stream.write<float>(fire.aimY);
  return stream;
}

inline socketwire::BitStream make_snapshot(const WorldSnapshot& snapshot)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Snapshot));
  stream.write<std::uint32_t>(snapshot.tick);
  stream.write<std::uint16_t>(static_cast<std::uint16_t>(snapshot.players.size()));
  for (const auto& player : snapshot.players)
  {
    stream.write<std::uint16_t>(player.id);
    stream.write<float>(player.x);
    stream.write<float>(player.y);
  }

  stream.write<std::uint16_t>(static_cast<std::uint16_t>(snapshot.projectiles.size()));
  for (const auto& projectile : snapshot.projectiles)
  {
    stream.write<std::uint16_t>(projectile.id);
    stream.write<std::uint16_t>(projectile.ownerId);
    stream.write<float>(projectile.x);
    stream.write<float>(projectile.y);
  }
  return stream;
}

inline bool read_type(socketwire::BitStream& stream, MessageType& type)
{
  const auto value = stream.try_read<std::uint8_t>();
  if (!value)
    return false;
  type = static_cast<MessageType>(*value);
  return true;
}

inline bool read_welcome(socketwire::BitStream& stream, std::uint16_t& playerId)
{
  const auto value = stream.try_read<std::uint16_t>();
  if (!value)
    return false;
  playerId = *value;
  return true;
}

inline bool read_input(socketwire::BitStream& stream, InputState& input)
{
  const auto tick = stream.try_read<std::uint32_t>();
  const auto axisX = stream.try_read<float>();
  const auto axisY = stream.try_read<float>();
  if (!tick || !axisX || !axisY)
    return false;
  input = InputState{*tick, *axisX, *axisY};
  return true;
}

inline bool read_fire(socketwire::BitStream& stream, FireCommand& fire)
{
  const auto tick = stream.try_read<std::uint32_t>();
  const auto aimX = stream.try_read<float>();
  const auto aimY = stream.try_read<float>();
  if (!tick || !aimX || !aimY)
    return false;
  fire = FireCommand{*tick, *aimX, *aimY};
  return true;
}

inline bool read_snapshot(socketwire::BitStream& stream, WorldSnapshot& snapshot)
{
  const auto tick = stream.try_read<std::uint32_t>();
  const auto playerCount = stream.try_read<std::uint16_t>();
  if (!tick || !playerCount)
    return false;

  snapshot = {};
  snapshot.tick = *tick;
  snapshot.players.reserve(*playerCount);
  for (std::uint16_t i = 0; i < *playerCount; ++i)
  {
    const auto id = stream.try_read<std::uint16_t>();
    const auto x = stream.try_read<float>();
    const auto y = stream.try_read<float>();
    if (!id || !x || !y)
      return false;
    snapshot.players.push_back(PlayerSnapshot{*id, *x, *y});
  }

  const auto projectileCount = stream.try_read<std::uint16_t>();
  if (!projectileCount)
    return false;

  snapshot.projectiles.reserve(*projectileCount);
  for (std::uint16_t i = 0; i < *projectileCount; ++i)
  {
    const auto id = stream.try_read<std::uint16_t>();
    const auto ownerId = stream.try_read<std::uint16_t>();
    const auto x = stream.try_read<float>();
    const auto y = stream.try_read<float>();
    if (!id || !ownerId || !x || !y)
      return false;
    snapshot.projectiles.push_back(ProjectileSnapshot{*id, *ownerId, *x, *y});
  }
  return true;
}

} // namespace projectile_arena

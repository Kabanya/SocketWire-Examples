#pragma once

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "reliable_connection.hpp"

#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#define SOCKETWIRE_EXAMPLES_HAS_GETRUSAGE 1
#endif

namespace socketwire_examples::benchmark {

struct Options {
  bool enabled = false;
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  std::uint16_t lobbyPort = 10887;
  std::uint16_t gamePort = 10888;
  int durationMs = 60000;
  int warmupMs = 5000;
  std::uint32_t seed = 1;
  std::string metricsPath;
  std::string metricsMode = "samples";
  int clients = 1;
  int run = 0;
};

struct NetworkStats {
  double rttMs = 0.0;
  std::uint64_t lostPackets = 0;
  std::uint64_t inflightPackets = 0;
  std::uint64_t sendWindow = 0;
};

struct GameMetrics {
  std::uint64_t appSentPackets = 0;
  std::uint64_t appReceivedPackets = 0;
  std::uint64_t appLostPackets = 0;
  std::uint64_t appDuplicatePackets = 0;
  std::uint64_t appReorderedPackets = 0;
  std::uint64_t joinSuccessCount = 0;
  double lobbyConvergenceTimeMs = 0.0;
  std::uint64_t lobbyStateMismatchCount = 0;
  std::uint64_t ghostPlayerCount = 0;
  std::uint64_t stateDivergenceCount = 0;
  std::uint64_t permanentDesyncCount = 0;
  std::uint64_t nanPositionCount = 0;
  std::uint64_t infPositionCount = 0;
  std::uint64_t invalidEntityStateCount = 0;
  double predictionErrorP95 = 0.0;
  double predictionErrorMax = 0.0;
  std::uint64_t correctionCount = 0;
  double snapshotAgeMs = 0.0;
  std::uint64_t interpolationUnderflowCount = 0;
  std::uint64_t entityCountServer = 0;
  std::uint64_t entityCountClient = 0;
  std::uint64_t missingEntityCount = 0;
  std::uint64_t ghostEntityCount = 0;
  std::uint64_t fireCommandSent = 0;
  std::uint64_t fireCommandAccepted = 0;
  std::uint64_t projectileSpawnCountServer = 0;
  std::uint64_t projectileSpawnCountClient = 0;
  std::uint64_t duplicateProjectileCount = 0;
  std::uint64_t duplicateHitEventCount = 0;
  std::uint64_t ghostProjectileCount = 0;
  std::uint64_t malformedPacketsAccepted = 0;
  std::uint64_t tamperedPacketsAccepted = 0;
  std::uint64_t invalidHandshakesAccepted = 0;
};

class MetricsCollector;
inline MetricsCollector*& ActiveCollector();

inline bool ParseInt(const char* text, int& out) {
  if (text == nullptr || *text == '\0') return false;

  const std::string_view view(text);
  int value = 0;
  const auto [end, error] =
    std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc{} || end != view.data() + view.size()) return false;

  out = value;
  return true;
}

inline bool ParseUInt16(const char* text, std::uint16_t& out) {
  int value = 0;
  if (!ParseInt(text, value) || value <= 0 || value > 65535) return false;

  out = static_cast<std::uint16_t>(value);
  return true;
}

inline Options ParseOptions(int argc, const char** argv,
                            std::uint16_t default_port,
                            std::uint16_t default_lobby_port = 10887,
                            std::uint16_t default_game_port = 10888) {
  Options options;
  options.port = default_port;
  options.lobbyPort = default_lobby_port;
  options.gamePort = default_game_port;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--bench") == 0) {
      options.enabled = true;
    } else if (std::strcmp(arg, "--host") == 0 && i + 1 < argc) {
      options.host = argv[++i];
    } else if (std::strcmp(arg, "--port") == 0 && i + 1 < argc) {
      ParseUInt16(argv[++i], options.port);
    } else if (std::strcmp(arg, "--lobby-port") == 0 && i + 1 < argc) {
      ParseUInt16(argv[++i], options.lobbyPort);
    } else if (std::strcmp(arg, "--game-port") == 0 && i + 1 < argc) {
      ParseUInt16(argv[++i], options.gamePort);
    } else if (std::strcmp(arg, "--duration-ms") == 0 && i + 1 < argc) {
      ParseInt(argv[++i], options.durationMs);
    } else if (std::strcmp(arg, "--warmup-ms") == 0 && i + 1 < argc) {
      ParseInt(argv[++i], options.warmupMs);
    } else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc) {
      int seed = 1;
      if (ParseInt(argv[++i], seed)) {
        options.seed = static_cast<std::uint32_t>(seed);
      }
    } else if (std::strcmp(arg, "--metrics") == 0 && i + 1 < argc) {
      options.metricsPath = argv[++i];
    } else if (std::strcmp(arg, "--metrics-mode") == 0 && i + 1 < argc) {
      options.metricsMode = argv[++i];
    } else if (std::strcmp(arg, "--clients") == 0 && i + 1 < argc) {
      ParseInt(argv[++i], options.clients);
    } else if (std::strcmp(arg, "--run") == 0 && i + 1 < argc) {
      ParseInt(argv[++i], options.run);
    }
  }

  if (options.durationMs <= 0) options.durationMs = 60000;
  if (options.warmupMs < 0) options.warmupMs = 0;
  if (options.clients <= 0) options.clients = 1;
  if (options.metricsMode != "summary") options.metricsMode = "samples";

  return options;
}

inline float DeterministicAxis(std::uint32_t seed, std::uint64_t frame,
                               std::uint32_t axis) {
  const auto phase = static_cast<std::uint32_t>(
    (frame / 30 + static_cast<std::uint64_t>(seed * 17) +
     static_cast<std::uint64_t>(axis * 7)) %
    4);
  switch (phase) {
    case 0:
      return 1.f;
    case 1:
      return 0.f;
    case 2:
      return -1.f;
    default:
      return 0.f;
  }
}

inline double NowCpuSeconds() {
#if defined(SOCKETWIRE_EXAMPLES_HAS_GETRUSAGE)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
  const double user = static_cast<double>(usage.ru_utime.tv_sec) +
                      static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0;
  const double system = static_cast<double>(usage.ru_stime.tv_sec) +
                        static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;
  return user + system;
#else
  return 0.0;
#endif
}

inline std::uint64_t CurrentRssKb() {
#if defined(SOCKETWIRE_EXAMPLES_HAS_GETRUSAGE)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss / 1024);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#endif
#else
  return 0;
#endif
}

class MetricsCollector {
 public:
  MetricsCollector(Options options, const char* example, const char* backend,
                   const char* role)
      : options_(std::move(options)),
        example_(example),
        backend_(backend),
        role_(role),
        start_(Clock::now()),
        lastSample_(start_),
        lastCpuSeconds_(NowCpuSeconds()) {
    if (!options_.enabled) return;

    if (!options_.metricsPath.empty()) {
      file_ = std::fopen(options_.metricsPath.c_str(), "a");
    }
    if (file_ == nullptr) file_ = stdout;
  }

  ~MetricsCollector() noexcept {
    try {
      Finish();
    } catch (...) {
      std::fputs("MetricsCollector::Finish failed\n", stderr);
    }
    if (ActiveCollector() == this) ActiveCollector() = nullptr;
  }

  [[nodiscard]] bool Enabled() const { return options_.enabled; }
  [[nodiscard]] const Options& GetOptions() const { return options_; }

  [[nodiscard]] bool Done() const {
    if (!options_.enabled) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           Clock::now() - start_)
                           .count();
    return elapsed >= static_cast<int>(options_.warmupMs + options_.durationMs);
  }

  bool Measuring() {
    UpdateMeasurementState(Clock::now());
    return measuring_;
  }

  void SetConnectedClients(int count) { connectedClients_ = count; }
  void SetNetworkStats(NetworkStats stats) { networkStats_ = stats; }
  void SetGameMetrics(GameMetrics metrics) { gameMetrics_ = metrics; }

  void RecordPayloadTx(std::size_t bytes) {
    if (!Measuring()) return;
    payloadTxBytes_ += bytes;
    payloadTxPackets_ += 1;
  }

  void RecordPayloadRx(std::size_t bytes) {
    if (!Measuring()) return;
    payloadRxBytes_ += bytes;
    payloadRxPackets_ += 1;
  }

  void RecordFrameMs(double ms) {
    if (!Measuring()) return;
    frameMsSum_ += ms;
    frameSamples_ += 1;
  }

  void RecordUpdateMs(double ms) {
    if (!Measuring()) return;
    updateMsSum_ += ms;
    updateSamples_ += 1;
  }

  void MaybeWriteSample() {
    if (!options_.enabled) return;

    const auto now = Clock::now();
    if (!UpdateMeasurementState(now)) return;

    if (options_.metricsMode == "summary") return;

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample_)
          .count() < 1000) {
      return;
    }

    WriteSample(now);
  }

  void Finish() {
    if (finished_) return;
    finished_ = true;

    if (options_.enabled && measuring_) WriteSample(Clock::now());

    if (file_ != nullptr && file_ != stdout) std::fclose(file_);
    file_ = nullptr;
  }

 private:
  using Clock = std::chrono::steady_clock;

  bool UpdateMeasurementState(Clock::time_point now) {
    if (!options_.enabled) return false;
    if (measuring_) return true;

    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_)
        .count();
    if (elapsed < options_.warmupMs) return false;

    measuring_ = true;
    measurementStart_ = now;
    lastSample_ = now;
    lastCpuSeconds_ = NowCpuSeconds();
    return true;
  }

  void WriteSample(Clock::time_point now) {
    if (file_ == nullptr) return;

    const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            measurementStart_)
        .count();
    const auto sample_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample_)
        .count();
    const double sample_seconds =
      sample_ms > 0 ? static_cast<double>(sample_ms) / 1000.0 : 1.0;
    const double current_cpu = NowCpuSeconds();
    const double cpu_percent =
      ((current_cpu - lastCpuSeconds_) / sample_seconds) * 100.0;
    lastCpuSeconds_ = current_cpu;

    const double frame_avg =
      frameSamples_ > 0 ? frameMsSum_ / static_cast<double>(frameSamples_)
                        : 0.0;
    const double update_avg =
      updateSamples_ > 0 ? updateMsSum_ / static_cast<double>(updateSamples_)
                         : 0.0;

    std::println(
      file_,
      "{{\"example\":\"{}\",\"backend\":\"{}\",\"role\":\"{}\",\"clients\":{},"
      "\"run\":{},\"elapsed_ms\":{},\"connected_clients\":{},\"payload_tx_"
      "bytes\":{},\"payload_rx_bytes\":{},\"payload_tx_packets\":{},\"payload_"
      "rx_packets\":{},\"app_sent_packets\":{},\"app_received_packets\":{},"
      "\"app_lost_packets\":{},\"app_duplicate_packets\":{},\"app_reordered_"
      "packets\":{},\"rtt_ms\":{:.3f},\"lost_packets\":{},\"inflight_packets\":"
      "{},\"send_window\":{},\"join_success_count\":{},\"lobby_convergence_"
      "time_ms\":{:.3f},\"lobby_state_mismatch_count\":{},\"ghost_player_"
      "count\":{},\"state_divergence_count\":{},\"permanent_desync_count\":{},"
      "\"nan_position_count\":{},\"inf_position_count\":{},\"invalid_entity_"
      "state_count\":{},\"prediction_error_p95\":{:.6f},\"prediction_error_"
      "max\":{:.6f},\"correction_count\":{},\"snapshot_age_ms\":{:.3f},"
      "\"interpolation_underflow_count\":{},\"entity_count_server\":{},"
      "\"entity_count_client\":{},\"missing_entity_count\":{},\"ghost_entity_"
      "count\":{},\"fire_command_sent\":{},\"fire_command_accepted\":{},"
      "\"projectile_spawn_count_server\":{},\"projectile_spawn_count_client\":{"
      "},\"duplicate_projectile_count\":{},\"duplicate_hit_event_count\":{},"
      "\"ghost_projectile_count\":{},\"malformed_packets_accepted\":{},"
      "\"tampered_packets_accepted\":{},\"invalid_handshakes_accepted\":{},"
      "\"frame_ms_avg\":{:.6f},\"update_ms_avg\":{:.6f},\"cpu_percent\":{:.3f},"
      "\"rss_kb\":{}}}",
      example_, backend_, role_, options_.clients, options_.run,
      static_cast<std::int64_t>(elapsed_ms), connectedClients_,
      static_cast<std::uint64_t>(payloadTxBytes_),
      static_cast<std::uint64_t>(payloadRxBytes_),
      static_cast<std::uint64_t>(payloadTxPackets_),
      static_cast<std::uint64_t>(payloadRxPackets_),
      static_cast<std::uint64_t>(gameMetrics_.appSentPackets),
      static_cast<std::uint64_t>(gameMetrics_.appReceivedPackets),
      static_cast<std::uint64_t>(gameMetrics_.appLostPackets),
      static_cast<std::uint64_t>(gameMetrics_.appDuplicatePackets),
      static_cast<std::uint64_t>(gameMetrics_.appReorderedPackets),
      networkStats_.rttMs,
      static_cast<std::uint64_t>(networkStats_.lostPackets),
      static_cast<std::uint64_t>(networkStats_.inflightPackets),
      static_cast<std::uint64_t>(networkStats_.sendWindow),
      static_cast<std::uint64_t>(gameMetrics_.joinSuccessCount),
      gameMetrics_.lobbyConvergenceTimeMs,
      static_cast<std::uint64_t>(gameMetrics_.lobbyStateMismatchCount),
      static_cast<std::uint64_t>(gameMetrics_.ghostPlayerCount),
      static_cast<std::uint64_t>(gameMetrics_.stateDivergenceCount),
      static_cast<std::uint64_t>(gameMetrics_.permanentDesyncCount),
      static_cast<std::uint64_t>(gameMetrics_.nanPositionCount),
      static_cast<std::uint64_t>(gameMetrics_.infPositionCount),
      static_cast<std::uint64_t>(gameMetrics_.invalidEntityStateCount),
      gameMetrics_.predictionErrorP95, gameMetrics_.predictionErrorMax,
      static_cast<std::uint64_t>(gameMetrics_.correctionCount),
      gameMetrics_.snapshotAgeMs,
      static_cast<std::uint64_t>(gameMetrics_.interpolationUnderflowCount),
      static_cast<std::uint64_t>(gameMetrics_.entityCountServer),
      static_cast<std::uint64_t>(gameMetrics_.entityCountClient),
      static_cast<std::uint64_t>(gameMetrics_.missingEntityCount),
      static_cast<std::uint64_t>(gameMetrics_.ghostEntityCount),
      static_cast<std::uint64_t>(gameMetrics_.fireCommandSent),
      static_cast<std::uint64_t>(gameMetrics_.fireCommandAccepted),
      static_cast<std::uint64_t>(gameMetrics_.projectileSpawnCountServer),
      static_cast<std::uint64_t>(gameMetrics_.projectileSpawnCountClient),
      static_cast<std::uint64_t>(gameMetrics_.duplicateProjectileCount),
      static_cast<std::uint64_t>(gameMetrics_.duplicateHitEventCount),
      static_cast<std::uint64_t>(gameMetrics_.ghostProjectileCount),
      static_cast<std::uint64_t>(gameMetrics_.malformedPacketsAccepted),
      static_cast<std::uint64_t>(gameMetrics_.tamperedPacketsAccepted),
      static_cast<std::uint64_t>(gameMetrics_.invalidHandshakesAccepted),
      frame_avg, update_avg, cpu_percent, CurrentRssKb());
    std::fflush(file_);

    lastSample_ = now;
    frameMsSum_ = 0.0;
    updateMsSum_ = 0.0;
    frameSamples_ = 0;
    updateSamples_ = 0;
  }

  Options options_;
  const char* example_ = "";
  const char* backend_ = "";
  const char* role_ = "";
  FILE* file_ = nullptr;
  Clock::time_point start_{};
  Clock::time_point measurementStart_{};
  Clock::time_point lastSample_{};
  double lastCpuSeconds_ = 0.0;
  bool measuring_ = false;
  bool finished_ = false;

  int connectedClients_ = 0;
  NetworkStats networkStats_;
  GameMetrics gameMetrics_;
  std::uint64_t payloadTxBytes_ = 0;
  std::uint64_t payloadRxBytes_ = 0;
  std::uint64_t payloadTxPackets_ = 0;
  std::uint64_t payloadRxPackets_ = 0;
  double frameMsSum_ = 0.0;
  double updateMsSum_ = 0.0;
  std::uint64_t frameSamples_ = 0;
  std::uint64_t updateSamples_ = 0;
};

inline MetricsCollector*& ActiveCollector() {
  static MetricsCollector* collector = nullptr;
  return collector;
}

inline void SetActiveCollector(MetricsCollector* collector) {
  ActiveCollector() = collector;
}

inline void RecordPayloadTx(std::size_t bytes) {
  if (ActiveCollector() != nullptr) ActiveCollector()->RecordPayloadTx(bytes);
}

inline void RecordPayloadRx(std::size_t bytes) {
  if (ActiveCollector() != nullptr) ActiveCollector()->RecordPayloadRx(bytes);
}

inline NetworkStats StatsFromConnection(
  const socketwire::ReliableConnection& connection) {
  NetworkStats stats;
  stats.rttMs = connection.GetRtt();
  stats.lostPackets = connection.GetLostPackets();
  stats.inflightPackets = connection.GetInflightCount();
  stats.sendWindow = connection.GetSendWindow();
  return stats;
}

template <typename ClientRange>
inline NetworkStats StatsFromClients(const ClientRange& clients) {
  NetworkStats stats;
  std::uint64_t connected = 0;
  for (auto* client : clients) {
    if (client == nullptr || client->connection == nullptr ||
        !client->connection->IsConnected()) {
      continue;
    }

    stats.rttMs += client->connection->GetRtt();
    stats.lostPackets += client->connection->GetLostPackets();
    stats.inflightPackets += client->connection->GetInflightCount();
    stats.sendWindow += client->connection->GetSendWindow();
    connected += 1;
  }

  if (connected > 0) stats.rttMs /= static_cast<double>(connected);
  return stats;
}

class ScopeTimer {
 public:
  enum class Target : std::uint8_t { kFrame, kUpdate };

  ScopeTimer(MetricsCollector& collector, Target target)
      : collector_(&collector),
        target_(target),
        start_(std::chrono::steady_clock::now()) {}

  ~ScopeTimer() {
    const auto end = std::chrono::steady_clock::now();
    const double ms =
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_)
          .count()) /
      1000.0;
    if (target_ == Target::kFrame) {
      collector_->RecordFrameMs(ms);
    } else {
      collector_->RecordUpdateMs(ms);
    }
  }

 private:
  MetricsCollector* collector_ = nullptr;
  Target target_;
  std::chrono::steady_clock::time_point start_{};
};

}  // namespace socketwire_examples::benchmark

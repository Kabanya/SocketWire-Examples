#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

inline bool parseInt(const char* text, int& out) {
  if (text == nullptr || *text == '\0') return false;

  char* end = nullptr;
  const int64_t value = std::strtol(text, &end, 10);
  if (end == text || *end != '\0') return false;

  out = static_cast<int>(value);
  return true;
}

inline bool parseUInt16(const char* text, std::uint16_t& out) {
  int value = 0;
  if (!parseInt(text, value) || value <= 0 || value > 65535) return false;

  out = static_cast<std::uint16_t>(value);
  return true;
}

inline Options parseOptions(int argc, const char** argv,
                            std::uint16_t defaultPort,
                            std::uint16_t defaultLobbyPort = 10887,
                            std::uint16_t defaultGamePort = 10888) {
  Options options;
  options.port = defaultPort;
  options.lobbyPort = defaultLobbyPort;
  options.gamePort = defaultGamePort;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--bench") == 0) {
      options.enabled = true;
    } else if (std::strcmp(arg, "--host") == 0 && i + 1 < argc) {
      options.host = argv[++i];
    } else if (std::strcmp(arg, "--port") == 0 && i + 1 < argc) {
      parseUInt16(argv[++i], options.port);
    } else if (std::strcmp(arg, "--lobby-port") == 0 && i + 1 < argc) {
      parseUInt16(argv[++i], options.lobbyPort);
    } else if (std::strcmp(arg, "--game-port") == 0 && i + 1 < argc) {
      parseUInt16(argv[++i], options.gamePort);
    } else if (std::strcmp(arg, "--duration-ms") == 0 && i + 1 < argc) {
      parseInt(argv[++i], options.durationMs);
    } else if (std::strcmp(arg, "--warmup-ms") == 0 && i + 1 < argc) {
      parseInt(argv[++i], options.warmupMs);
    } else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc) {
      int seed = 1;
      if (parseInt(argv[++i], seed))
        options.seed = static_cast<std::uint32_t>(seed);
    } else if (std::strcmp(arg, "--metrics") == 0 && i + 1 < argc) {
      options.metricsPath = argv[++i];
    } else if (std::strcmp(arg, "--metrics-mode") == 0 && i + 1 < argc) {
      options.metricsMode = argv[++i];
    } else if (std::strcmp(arg, "--clients") == 0 && i + 1 < argc) {
      parseInt(argv[++i], options.clients);
    } else if (std::strcmp(arg, "--run") == 0 && i + 1 < argc) {
      parseInt(argv[++i], options.run);
    }
  }

  if (options.durationMs <= 0) options.durationMs = 60000;
  if (options.warmupMs < 0) options.warmupMs = 0;
  if (options.clients <= 0) options.clients = 1;
  if (options.metricsMode != "summary") options.metricsMode = "samples";

  return options;
}

inline float deterministicAxis(std::uint32_t seed, std::uint64_t frame,
                               std::uint32_t axis) {
  const auto phase =
    static_cast<std::uint32_t>((frame / 30 + seed * 17 + axis * 7) % 4);
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

inline double nowCpuSeconds() {
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

inline std::uint64_t currentRssKb() {
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
        lastCpuSeconds_(nowCpuSeconds()) {
    if (!options_.enabled) return;

    if (!options_.metricsPath.empty())
      file_ = std::fopen(options_.metricsPath.c_str(), "a");
    if (file_ == nullptr) file_ = stdout;
  }

  ~MetricsCollector() { finish(); }

  bool enabled() const { return options_.enabled; }
  const Options& options() const { return options_; }

  bool done() const {
    if (!options_.enabled) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           Clock::now() - start_)
                           .count();
    return elapsed >= static_cast<int>(options_.warmupMs + options_.durationMs);
  }

  bool measuring() {
    updateMeasurementState(Clock::now());
    return measuring_;
  }

  void setConnectedClients(int count) { connectedClients_ = count; }
  void setNetworkStats(NetworkStats stats) { networkStats_ = stats; }

  void recordPayloadTx(std::size_t bytes) {
    if (!measuring()) return;
    payloadTxBytes_ += bytes;
    payloadTxPackets_ += 1;
  }

  void recordPayloadRx(std::size_t bytes) {
    if (!measuring()) return;
    payloadRxBytes_ += bytes;
    payloadRxPackets_ += 1;
  }

  void recordFrameMs(double ms) {
    if (!measuring()) return;
    frameMsSum_ += ms;
    frameSamples_ += 1;
  }

  void recordUpdateMs(double ms) {
    if (!measuring()) return;
    updateMsSum_ += ms;
    updateSamples_ += 1;
  }

  void maybeWriteSample() {
    if (!options_.enabled) return;

    const auto now = Clock::now();
    if (!updateMeasurementState(now)) return;

    if (options_.metricsMode == "summary") return;

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample_)
          .count() < 1000)
      return;

    writeSample(now);
  }

  void finish() {
    if (finished_) return;
    finished_ = true;

    if (options_.enabled && measuring_) writeSample(Clock::now());

    if (file_ != nullptr && file_ != stdout) std::fclose(file_);
    file_ = nullptr;
  }

 private:
  using Clock = std::chrono::steady_clock;

  bool updateMeasurementState(Clock::time_point now) {
    if (!options_.enabled) return false;
    if (measuring_) return true;

    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_)
        .count();
    if (elapsed < options_.warmupMs) return false;

    measuring_ = true;
    measurementStart_ = now;
    lastSample_ = now;
    lastCpuSeconds_ = nowCpuSeconds();
    return true;
  }

  void writeSample(Clock::time_point now) {
    if (file_ == nullptr) return;

    const auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            measurementStart_)
        .count();
    const auto sampleMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample_)
        .count();
    const double sampleSeconds =
      sampleMs > 0 ? static_cast<double>(sampleMs) / 1000.0 : 1.0;
    const double currentCpu = nowCpuSeconds();
    const double cpuPercent =
      ((currentCpu - lastCpuSeconds_) / sampleSeconds) * 100.0;
    lastCpuSeconds_ = currentCpu;

    const double frameAvg = frameSamples_ > 0
                              ? frameMsSum_ / static_cast<double>(frameSamples_)
                              : 0.0;
    const double updateAvg =
      updateSamples_ > 0 ? updateMsSum_ / static_cast<double>(updateSamples_)
                         : 0.0;

    std::fprintf(
      file_,
      "{\"example\":\"%s\",\"backend\":\"%s\",\"role\":\"%s\","
      "\"clients\":%d,\"run\":%d,\"elapsed_ms\":%lld,"
      "\"connected_clients\":%d,"
      "\"payload_tx_bytes\":%llu,\"payload_rx_bytes\":%llu,"
      "\"payload_tx_packets\":%llu,\"payload_rx_packets\":%llu,"
      "\"rtt_ms\":%.3f,\"lost_packets\":%llu,"
      "\"inflight_packets\":%llu,\"send_window\":%llu,"
      "\"frame_ms_avg\":%.6f,\"update_ms_avg\":%.6f,"
      "\"cpu_percent\":%.3f,\"rss_kb\":%llu}\n",
      example_, backend_, role_, options_.clients, options_.run,
      static_cast<long long>(elapsedMs), connectedClients_,
      static_cast<unsigned long long>(payloadTxBytes_),
      static_cast<unsigned long long>(payloadRxBytes_),
      static_cast<unsigned long long>(payloadTxPackets_),
      static_cast<unsigned long long>(payloadRxPackets_), networkStats_.rttMs,
      static_cast<unsigned long long>(networkStats_.lostPackets),
      static_cast<unsigned long long>(networkStats_.inflightPackets),
      static_cast<unsigned long long>(networkStats_.sendWindow), frameAvg,
      updateAvg, cpuPercent, static_cast<unsigned long long>(currentRssKb()));
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
  Clock::time_point start_;
  Clock::time_point measurementStart_;
  Clock::time_point lastSample_;
  double lastCpuSeconds_ = 0.0;
  bool measuring_ = false;
  bool finished_ = false;

  int connectedClients_ = 0;
  NetworkStats networkStats_;
  std::uint64_t payloadTxBytes_ = 0;
  std::uint64_t payloadRxBytes_ = 0;
  std::uint64_t payloadTxPackets_ = 0;
  std::uint64_t payloadRxPackets_ = 0;
  double frameMsSum_ = 0.0;
  double updateMsSum_ = 0.0;
  std::uint64_t frameSamples_ = 0;
  std::uint64_t updateSamples_ = 0;
};

inline MetricsCollector*& activeCollector() {
  static MetricsCollector* collector = nullptr;
  return collector;
}

inline void setActiveCollector(MetricsCollector* collector) {
  activeCollector() = collector;
}

inline void recordPayloadTx(std::size_t bytes) {
  if (activeCollector() != nullptr) activeCollector()->recordPayloadTx(bytes);
}

inline void recordPayloadRx(std::size_t bytes) {
  if (activeCollector() != nullptr) activeCollector()->recordPayloadRx(bytes);
}

inline NetworkStats statsFromConnection(
  const socketwire::ReliableConnection& connection) {
  NetworkStats stats;
  stats.rttMs = connection.GetRtt();
  stats.lostPackets = connection.GetLostPackets();
  stats.inflightPackets = connection.GetInflightCount();
  stats.sendWindow = connection.GetSendWindow();
  return stats;
}

template <typename ClientRange>
inline NetworkStats statsFromClients(const ClientRange& clients) {
  NetworkStats stats;
  std::uint64_t connected = 0;
  for (auto* client : clients) {
    if (client == nullptr || client->connection == nullptr ||
        !client->connection->IsConnected())
      continue;

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
  enum class Target { Frame, Update };

  ScopeTimer(MetricsCollector& collector, Target target)
      : collector_(collector),
        target_(target),
        start_(std::chrono::steady_clock::now()) {}

  ~ScopeTimer() {
    const auto end = std::chrono::steady_clock::now();
    const double ms =
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_)
          .count()) /
      1000.0;
    if (target_ == Target::Frame)
      collector_.recordFrameMs(ms);
    else
      collector_.recordUpdateMs(ms);
  }

 private:
  MetricsCollector& collector_;
  Target target_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace socketwire_examples::benchmark

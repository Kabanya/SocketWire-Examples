#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#define NETBENCH_HAS_GETRUSAGE 1
#endif

namespace netbench {

using Clock = std::chrono::steady_clock;

constexpr std::uint16_t kDefaultPort = 53490;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kTransportPacketSize = 1400;
constexpr std::size_t kMaxPayloadSize = 4096;
constexpr std::array<std::uint8_t, 4> kMagic{'S', 'W', 'N', 'B'};

enum class PacketKind : std::uint8_t { kData = 1, kEcho = 2 };

enum class DeliveryMode : std::uint8_t {
  kReliable = 1,
  kUnreliable = 2,
  kUnsequenced = 3,
  kSequenced = 4,
  kDeadlineReliable = 5,
  kDeadlineUnreliable = 6,
  kDeadlineUnsequenced = 7,
  kDeadlineSequenced = 8,
};

enum class Bucket : std::uint8_t {
  kReliable = 0,
  kUnreliable = 1,
  kUnsequenced = 2,
  kSequenced = 3,
  kDeadlineReliable = 4,
  kDeadlineUnreliable = 5,
  kDeadlineUnsequenced = 6,
  kDeadlineSequenced = 7,
  kCount = 8,
};

struct PacketHeader {
  PacketKind kind = PacketKind::kData;
  DeliveryMode mode = DeliveryMode::kReliable;
  std::uint8_t channel = 0;
  std::uint32_t clientId = 0;
  std::uint32_t sequence = 0;
  std::uint64_t sentUs = 0;
  std::uint32_t bodySize = 0;
  std::uint32_t checksum = 0;
};

struct Options {
  std::string host = "127.0.0.1";
  std::uint16_t port = kDefaultPort;
  int clients = 1;
  int durationMs = 60000;
  int warmupMs = 5000;
  int drainMs = 1000;
  int run = 0;
  int serverWorkers = 1;
  int serverMaxClients = 0;
  std::uint32_t seed = 1;
  std::string profile = "mixed_latency";
  std::string metricsPath;
  std::string metricsMode = "samples";
};

struct TrafficProfile {
  std::string_view name;
  std::uint32_t reliablePps = 5;
  std::uint32_t unreliablePps = 20;
  std::uint32_t unsequencedPps = 10;
  std::uint32_t sequencedPps = 0;
  std::uint32_t deadlinePps = 5;
  std::size_t reliableBytes = 128;
  std::size_t unreliableBytes = 64;
  std::size_t unsequencedBytes = 64;
  std::size_t sequencedBytes = 64;
  std::size_t deadlineBytes = 96;
};

struct ScheduledStream {
  std::uint32_t pps = 0;
  std::size_t bytes = kHeaderSize;
  std::uint64_t nextUs = 0;
  std::uint64_t intervalUs = 0;

  void Reset(std::uint64_t now_us) {
    intervalUs = pps == 0 ? 0 : 1000000ULL / pps;
    nextUs = now_us;
  }

  [[nodiscard]] bool Due(std::uint64_t now_us) const {
    return intervalUs > 0 && now_us >= nextUs;
  }

  void Advance() { nextUs += std::max<std::uint64_t>(intervalUs, 1); }
};

struct BucketCounters {
  std::uint64_t sent = 0;
  std::uint64_t echoed = 0;
};

struct AppStats {
  std::array<BucketCounters, static_cast<std::size_t>(Bucket::kCount)>
    buckets{};
  std::uint64_t payloadTxBytes = 0;
  std::uint64_t payloadRxBytes = 0;
  std::uint64_t sendFailures = 0;
  std::uint64_t connectFailures = 0;
  std::uint64_t malformedPackets = 0;
  std::uint64_t corruptedPackets = 0;
  double updateMsSum = 0.0;
  double updateMsMax = 0.0;
  std::uint64_t updateSamples = 0;

  void NoteSent(Bucket bucket, std::size_t bytes) {
    buckets.at(static_cast<std::size_t>(bucket)).sent += 1;
    payloadTxBytes += bytes;
  }

  void NoteEchoed(Bucket bucket, std::size_t bytes) {
    buckets.at(static_cast<std::size_t>(bucket)).echoed += 1;
    payloadRxBytes += bytes;
  }

  void NoteUpdateMs(double ms) {
    updateMsSum += ms;
    updateMsMax = std::max(updateMsMax, ms);
    updateSamples += 1;
  }

  [[nodiscard]] double UpdateAvgMs() const {
    return updateSamples == 0 ? 0.0 : updateMsSum / static_cast<double>(updateSamples);
  }

  void ResetInterval() {
    updateMsSum = 0.0;
    updateMsMax = 0.0;
    updateSamples = 0;
  }
};

struct TransportStats {
  double rttMs = 0.0;
  std::uint64_t LostPackets = 0;
  std::uint64_t inflightPackets = 0;
  std::uint64_t sendWindow = 0;
  std::uint64_t deadlineSendDrops = 0;
  std::uint64_t deadlineReceiveDrops = 0;
  std::uint64_t deadlineRetriesPrevented = 0;
  std::uint64_t deadlineExpiredFragmentGroups = 0;
};

struct ProcessStats {
  int clientsRequested = 1;
  int clientsCreated = 1;
  int connectedClients = 0;
  int serverWorkers = 1;
  bool reusePort = false;
  int workerConnectedMin = 0;
  int workerConnectedMax = 0;
  double workerUpdateMsAvg = 0.0;
  double workerUpdateMsMax = 0.0;
  std::string_view status = "running";
  TransportStats transport{};
};

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
                            std::uint16_t default_port = kDefaultPort) {
  Options options;
  options.port = default_port;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--host") == 0 && i + 1 < argc) {
      options.host = argv[++i];
    } else if (std::strcmp(arg, "--port") == 0 && i + 1 < argc) {
      (void)ParseUInt16(argv[++i], options.port);
    } else if (std::strcmp(arg, "--clients") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.clients);
    } else if (std::strcmp(arg, "--duration-ms") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.durationMs);
    } else if (std::strcmp(arg, "--warmup-ms") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.warmupMs);
    } else if (std::strcmp(arg, "--drain-ms") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.drainMs);
    } else if (std::strcmp(arg, "--run") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.run);
    } else if (std::strcmp(arg, "--server-workers") == 0 && i + 1 < argc) {
      (void)ParseInt(argv[++i], options.serverWorkers);
    } else if (std::strcmp(arg, "--server-max-clients") == 0 &&
               i + 1 < argc) {
      (void)ParseInt(argv[++i], options.serverMaxClients);
    } else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc) {
      int seed = 1;
      if (ParseInt(argv[++i], seed)) {
        options.seed = static_cast<std::uint32_t>(std::max(seed, 1));
      }
    } else if (std::strcmp(arg, "--profile") == 0 && i + 1 < argc) {
      options.profile = argv[++i];
    } else if (std::strcmp(arg, "--metrics") == 0 && i + 1 < argc) {
      options.metricsPath = argv[++i];
    } else if (std::strcmp(arg, "--metrics-mode") == 0 && i + 1 < argc) {
      options.metricsMode = argv[++i];
    }
  }

  if (options.clients <= 0) options.clients = 1;
  if (options.durationMs <= 0) options.durationMs = 60000;
  if (options.warmupMs < 0) options.warmupMs = 0;
  if (options.drainMs < 0) options.drainMs = 0;
  if (options.serverWorkers <= 0) options.serverWorkers = 1;
  if (options.metricsMode != "summary") options.metricsMode = "samples";
  return options;
}

inline TrafficProfile ProfileByName(const std::string& name) {
  if (name == "reliable_small") {
    return {.name = "reliable_small",
            .reliablePps = 200,
            .unreliablePps = 0,
            .unsequencedPps = 0,
            .deadlinePps = 0,
            .reliableBytes = 128,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "reliable_fragmented") {
    return {.name = "reliable_fragmented",
            .reliablePps = 50,
            .unreliablePps = 0,
            .unsequencedPps = 0,
            .deadlinePps = 0,
            .reliableBytes = 4096,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "unreliable_small") {
    return {.name = "unreliable_small",
            .reliablePps = 0,
            .unreliablePps = 600,
            .unsequencedPps = 0,
            .deadlinePps = 0,
            .reliableBytes = 128,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "client_scalability") {
    return {.name = "client_scalability",
            .reliablePps = 30,
            .unreliablePps = 90,
            .unsequencedPps = 0,
            .deadlinePps = 0,
            .reliableBytes = 128,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "low_bandwidth") {
    return {.name = "low_bandwidth",
            .reliablePps = 10,
            .unreliablePps = 60,
            .unsequencedPps = 0,
            .deadlinePps = 0,
            .reliableBytes = 512,
            .unreliableBytes = 96,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "bad_wifi" || name == "normal_online" || name == "perfect_lan") {
    return {.name = "mixed_latency",
            .reliablePps = 20,
            .unreliablePps = 60,
            .unsequencedPps = 0,
            .deadlinePps = 2,
            .reliableBytes = 128,
            .unreliableBytes = 96,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "high_ping") {
    return {.name = "high_ping",
            .reliablePps = 15,
            .unreliablePps = 60,
            .unsequencedPps = 0,
            .deadlinePps = 2,
            .reliableBytes = 128,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "loss_10" || name == "burst_blackout") {
    return {.name = "loss_10",
            .reliablePps = 15,
            .unreliablePps = 40,
            .unsequencedPps = 0,
            .deadlinePps = 2,
            .reliableBytes = 128,
            .unreliableBytes = 64,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "small_mtu") {
    return {.name = "small_mtu",
            .reliablePps = 10,
            .unreliablePps = 20,
            .unsequencedPps = 0,
            .deadlinePps = 1,
            .reliableBytes = 1024,
            .unreliableBytes = 96,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "chaos") {
    return {.name = "chaos",
            .reliablePps = 20,
            .unreliablePps = 80,
            .unsequencedPps = 0,
            .deadlinePps = 2,
            .reliableBytes = 256,
            .unreliableBytes = 96,
            .unsequencedBytes = 64,
            .deadlineBytes = 96};
  }
  if (name == "flood") {
    return {.name = "flood",
            .reliablePps = 20,
            .unreliablePps = 200,
            .unsequencedPps = 100,
            .sequencedPps = 100,
            .deadlinePps = 60,
            .reliableBytes = 128,
            .unreliableBytes = 96,
            .unsequencedBytes = 96,
            .sequencedBytes = 96,
            .deadlineBytes = 96};
  }
  return {.name = "mixed_latency"};
}

inline std::uint64_t NowUs() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(
      Clock::now().time_since_epoch())
      .count());
}

inline std::uint32_t Checksum(const std::uint8_t* data, std::size_t size) {
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

inline void WriteU32(std::uint8_t* data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
  data[1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  data[2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  data[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

inline void WriteU64(std::uint8_t* data, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    data[7 - i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu);
  }
}

inline std::uint32_t ReadU32(const std::uint8_t* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) |
         (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) |
         static_cast<std::uint32_t>(data[3]);
}

inline std::uint64_t ReadU64(const std::uint8_t* data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value = (value << 8) | data[i];
  return value;
}

inline Bucket BucketForMode(DeliveryMode mode) {
  switch (mode) {
    case DeliveryMode::kReliable:
      return Bucket::kReliable;
    case DeliveryMode::kUnreliable:
      return Bucket::kUnreliable;
    case DeliveryMode::kUnsequenced:
      return Bucket::kUnsequenced;
    case DeliveryMode::kSequenced:
      return Bucket::kSequenced;
    case DeliveryMode::kDeadlineReliable:
      return Bucket::kDeadlineReliable;
    case DeliveryMode::kDeadlineUnreliable:
      return Bucket::kDeadlineUnreliable;
    case DeliveryMode::kDeadlineUnsequenced:
      return Bucket::kDeadlineUnsequenced;
    case DeliveryMode::kDeadlineSequenced:
      return Bucket::kDeadlineSequenced;
  }
  return Bucket::kReliable;
}

inline bool ValidMode(DeliveryMode mode) {
  switch (mode) {
    case DeliveryMode::kReliable:
    case DeliveryMode::kUnreliable:
    case DeliveryMode::kUnsequenced:
    case DeliveryMode::kSequenced:
    case DeliveryMode::kDeadlineReliable:
    case DeliveryMode::kDeadlineUnreliable:
    case DeliveryMode::kDeadlineUnsequenced:
    case DeliveryMode::kDeadlineSequenced:
      return true;
  }
  return false;
}

inline std::uint8_t ChannelForMode(DeliveryMode mode) {
  switch (mode) {
    case DeliveryMode::kReliable:
      return 0;
    case DeliveryMode::kUnreliable:
      return 1;
    case DeliveryMode::kUnsequenced:
      return 2;
    case DeliveryMode::kSequenced:
      return 3;
    case DeliveryMode::kDeadlineReliable:
      return 4;
    case DeliveryMode::kDeadlineUnreliable:
      return 5;
    case DeliveryMode::kDeadlineUnsequenced:
      return 6;
    case DeliveryMode::kDeadlineSequenced:
      return 7;
  }
  return 0;
}

inline std::size_t MakePayload(std::uint8_t* out, std::size_t requested_size,
                               PacketKind kind, DeliveryMode mode,
                               std::uint32_t client_id, std::uint32_t sequence,
                               std::uint32_t seed) {
  const std::size_t size =
    std::clamp(requested_size, kHeaderSize, kMaxPayloadSize);
  std::memcpy(out, kMagic.data(), kMagic.size());
  out[4] = static_cast<std::uint8_t>(kind);
  out[5] = static_cast<std::uint8_t>(mode);
  out[6] = ChannelForMode(mode);
  out[7] = 0;
  WriteU32(out + 8, client_id);
  WriteU32(out + 12, sequence);
  WriteU64(out + 16, NowUs());
  WriteU32(out + 24, static_cast<std::uint32_t>(size - kHeaderSize));

  for (std::size_t i = kHeaderSize; i < size; ++i) {
    out[i] = static_cast<std::uint8_t>(
      (seed + client_id * 31u + sequence * 17u + i * 13u) & 0xFFu);
  }

  WriteU32(out + 28, Checksum(out + kHeaderSize, size - kHeaderSize));
  return size;
}

inline bool ParseHeader(const void* data, std::size_t size,
                        PacketHeader& header) {
  if (data == nullptr || size < kHeaderSize) return false;

  const auto* bytes = static_cast<const std::uint8_t*>(data);
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes)) return false;

  header.kind = static_cast<PacketKind>(bytes[4]);
  header.mode = static_cast<DeliveryMode>(bytes[5]);
  header.channel = bytes[6];
  header.clientId = ReadU32(bytes + 8);
  header.sequence = ReadU32(bytes + 12);
  header.sentUs = ReadU64(bytes + 16);
  header.bodySize = ReadU32(bytes + 24);
  header.checksum = ReadU32(bytes + 28);

  if (header.kind != PacketKind::kData && header.kind != PacketKind::kEcho) {
    return false;
  }
  if (!ValidMode(header.mode)) return false;
  if (header.channel != ChannelForMode(header.mode)) return false;
  return header.bodySize == size - kHeaderSize;
}

inline bool ValidPayload(const PacketHeader& header, const void* data,
                         std::size_t size) {
  if (data == nullptr || size < kHeaderSize) return false;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  return header.checksum == Checksum(bytes + kHeaderSize, size - kHeaderSize);
}

inline double CpuSeconds() {
#if defined(NETBENCH_HAS_GETRUSAGE)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
  return static_cast<double>(usage.ru_utime.tv_sec) +
         static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0 +
         static_cast<double>(usage.ru_stime.tv_sec) +
         static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;
#else
  return 0.0;
#endif
}

inline std::uint64_t RssKb() {
#if defined(NETBENCH_HAS_GETRUSAGE)
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

class MetricsWriter {
 public:
  MetricsWriter(Options options, const char* role)
      : options_(std::move(options)),
        role_(role),
        start_(Clock::now()),
        lastSample_(start_),
        measurementStart_(start_),
        lastCpuSeconds_(CpuSeconds()) {
    if (!options_.metricsPath.empty()) {
      const std::filesystem::path path(options_.metricsPath);
      const auto parent = path.parent_path();
      if (!parent.empty()) std::filesystem::create_directories(parent);
      file_ = std::fopen(options_.metricsPath.c_str(), "a");
    }
  }

  ~MetricsWriter() {
    if (file_ != nullptr) std::fclose(file_);
  }

  [[nodiscard]] bool Measuring() const {
    const auto elapsed = ElapsedMs(Clock::now());
    return elapsed >= options_.warmupMs &&
           elapsed < options_.warmupMs + options_.durationMs;
  }

  [[nodiscard]] bool Done() const {
    return ElapsedMs(Clock::now()) >=
           options_.warmupMs + options_.durationMs + options_.drainMs;
  }

  void MaybeWriteSample(AppStats& stats, const ProcessStats& process) {
    if (options_.metricsMode == "summary") return;
    const auto now = Clock::now();
    if (ElapsedMs(now) < options_.warmupMs) return;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample_)
          .count() < 1000) {
      return;
    }
    Write("sample", stats, process, now);
    stats.ResetInterval();
    lastSample_ = now;
  }

  void Finish(AppStats& stats, ProcessStats process) {
    process.status = process.status.empty() ? "ok" : process.status;
    Write("final", stats, process, Clock::now());
  }

 private:
  [[nodiscard]] std::int64_t ElapsedMs(Clock::time_point now) const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_)
      .count();
  }

  static std::uint64_t Lost(const BucketCounters& counters) {
    return counters.sent > counters.echoed ? counters.sent - counters.echoed
                                           : 0;
  }

  void Write(const char* record, const AppStats& stats,
             const ProcessStats& process, Clock::time_point now) {
    if (ElapsedMs(now) >= options_.warmupMs && measurementStart_ == start_) {
      measurementStart_ = now;
      lastCpuSeconds_ = CpuSeconds();
    }

    const double current_cpu = CpuSeconds();
    const double sample_seconds =
      std::max(0.001, std::chrono::duration<double>(now - lastSample_).count());
    const double cpu_percent =
      ((current_cpu - lastCpuSeconds_) / sample_seconds) * 100.0;
    lastCpuSeconds_ = current_cpu;

    const auto& reliable =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kReliable));
    const auto& unreliable =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kUnreliable));
    const auto& unsequenced =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kUnsequenced));
    const auto& sequenced =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kSequenced));
    const auto& deadline_reliable =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kDeadlineReliable));
    const auto& deadline_unreliable =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kDeadlineUnreliable));
    const auto& deadline_unsequenced =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kDeadlineUnsequenced));
    const auto& deadline_sequenced =
      stats.buckets.at(static_cast<std::size_t>(Bucket::kDeadlineSequenced));
    const BucketCounters deadline{
      .sent = deadline_reliable.sent + deadline_unreliable.sent +
              deadline_unsequenced.sent + deadline_sequenced.sent,
      .echoed = deadline_reliable.echoed + deadline_unreliable.echoed +
                deadline_unsequenced.echoed + deadline_sequenced.echoed,
    };

    const auto print = [&](FILE* out) {
      if (out == nullptr) return;
      const auto json = std::format(
        "{{\"example\":\"network-bench\",\"backend\":\"socketwire\","
        "\"role\":\"{}\",\"record\":\"{}\",\"profile\":\"{}\",\"run\":{},"
        "\"elapsed_ms\":{},\"clients_requested\":{},\"clients_created\":{},"
        "\"connected_clients\":{},\"status\":\"{}\","
        "\"server_workers\":{},\"reuse_port\":{},"
        "\"worker_connected_min\":{},\"worker_connected_max\":{},"
        "\"worker_update_ms_avg\":{:.6f},"
        "\"worker_update_ms_max\":{:.6f},"
        "\"reliable_sent\":{},\"reliable_echoed\":{},\"reliable_Lost\":{},"
        "\"unreliable_sent\":{},\"unreliable_echoed\":{},"
        "\"unreliable_Lost\":{},\"unsequenced_sent\":{},"
        "\"unsequenced_echoed\":{},\"unsequenced_Lost\":{},"
        "\"sequenced_sent\":{},\"sequenced_echoed\":{},"
        "\"sequenced_Lost\":{},"
        "\"deadline_sent\":{},\"deadline_echoed\":{},\"deadline_Lost\":{},"
        "\"deadline_reliable_sent\":{},\"deadline_reliable_echoed\":{},"
        "\"deadline_reliable_Lost\":{},"
        "\"deadline_unreliable_sent\":{},\"deadline_unreliable_echoed\":{},"
        "\"deadline_unreliable_Lost\":{},"
        "\"deadline_unsequenced_sent\":{},"
        "\"deadline_unsequenced_echoed\":{},"
        "\"deadline_unsequenced_Lost\":{},"
        "\"deadline_sequenced_sent\":{},\"deadline_sequenced_echoed\":{},"
        "\"deadline_sequenced_Lost\":{},"
        "\"send_failures\":{},\"connect_failures\":{},"
        "\"malformed_packets\":{},\"corrupted_packets\":{},"
        "\"payload_tx_bytes\":{},\"payload_rx_bytes\":{},"
        "\"rtt_ms\":{:.3f},\"Lost_packets\":{},\"inflight_packets\":{},"
        "\"send_window\":{},\"deadline_send_drops\":{},"
        "\"deadline_receive_drops\":{},\"deadline_retries_prevented\":{},"
        "\"deadline_expired_fragment_groups\":{},"
        "\"update_ms_avg\":{:.6f},\"update_ms_max\":{:.6f},"
        "\"cpu_percent\":{:.3f},\"cpu_process_percent\":{:.3f},"
        "\"rss_kb\":{}}}",
        role_, record, options_.profile, options_.run, ElapsedMs(now),
        process.clientsRequested, process.clientsCreated,
        process.connectedClients, process.status, process.serverWorkers,
        process.reusePort, process.workerConnectedMin,
        process.workerConnectedMax, process.workerUpdateMsAvg,
        process.workerUpdateMsMax, reliable.sent, reliable.echoed,
        Lost(reliable), unreliable.sent, unreliable.echoed, Lost(unreliable),
        unsequenced.sent, unsequenced.echoed, Lost(unsequenced), sequenced.sent,
        sequenced.echoed, Lost(sequenced), deadline.sent, deadline.echoed,
        Lost(deadline), deadline_reliable.sent, deadline_reliable.echoed,
        Lost(deadline_reliable), deadline_unreliable.sent,
        deadline_unreliable.echoed, Lost(deadline_unreliable),
        deadline_unsequenced.sent, deadline_unsequenced.echoed,
        Lost(deadline_unsequenced), deadline_sequenced.sent,
        deadline_sequenced.echoed, Lost(deadline_sequenced), stats.sendFailures,
        stats.connectFailures, stats.malformedPackets, stats.corruptedPackets,
        stats.payloadTxBytes, stats.payloadRxBytes, process.transport.rttMs,
        process.transport.LostPackets, process.transport.inflightPackets,
        process.transport.sendWindow, process.transport.deadlineSendDrops,
        process.transport.deadlineReceiveDrops,
        process.transport.deadlineRetriesPrevented,
        process.transport.deadlineExpiredFragmentGroups, stats.UpdateAvgMs(),
        stats.updateMsMax, cpu_percent, cpu_percent, RssKb());
      std::fwrite(json.data(), 1, json.size(), out);
      std::fwrite("\n", 1, 1, out);
      std::fflush(out);
    };

    print(stdout);
    print(file_);
  }

  Options options_;
  const char* role_ = "";
  FILE* file_ = nullptr;
  Clock::time_point start_;
  Clock::time_point lastSample_;
  Clock::time_point measurementStart_;
  double lastCpuSeconds_ = 0.0;
};

}  // namespace netbench

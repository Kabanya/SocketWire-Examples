#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <latch>
#include <thread>

#include "task_queue.hpp"
#include "thread_pool.hpp"

namespace {

constexpr std::size_t kTaskCount = 6;

int CpuWork(int value) {
  int result = 0;
  for (int i = 0; i < 1000; ++i) result += (value + i) % 17;
  return result;
}

bool LowLevelThreadPoolDemo() {
  socketwire::ThreadPool workers(2);
  std::array<int, kTaskCount> results{};
  std::latch done(kTaskCount);

  workers.Start();
  bool submitted = true;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const bool accepted = workers.Submit([i, &results, &done] {
      results.at(i) = CpuWork(static_cast<int>(i));
      done.count_down();
    });
    submitted = submitted && accepted;
    if (!accepted) done.count_down();
  }

  done.wait();
  workers.Stop();

  bool ok = submitted;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const int expected = CpuWork(static_cast<int>(i));
    ok = ok && results.at(i) == expected;
    std::cout << "low-level task " << i << " -> " << results.at(i) << '\n';
  }
  return ok;
}

bool HighLevelOwnerThreadDemo() {
  socketwire::ThreadPool workers(2);
  socketwire::TaskQueue owner_queue;
  const auto owner_thread = std::this_thread::get_id();
  std::array<int, kTaskCount> results{};
  std::atomic<int> post_failures{0};
  bool wrong_thread = false;
  std::latch posted(kTaskCount);

  workers.Start();
  bool submitted = true;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const bool accepted = workers.Submit([i, &owner_queue, &posted,
                                          &post_failures, &results,
                                          &wrong_thread, owner_thread] {
      const int value = CpuWork(static_cast<int>(i));
      if (!owner_queue.Post([i, value, &results, &wrong_thread, owner_thread] {
            wrong_thread =
              wrong_thread || std::this_thread::get_id() != owner_thread;
            results.at(i) = value;
          })) {
        post_failures.fetch_add(1);
      }
      posted.count_down();
    });
    submitted = submitted && accepted;
    if (!accepted) posted.count_down();
  }

  posted.wait();
  workers.Stop();
  const std::size_t drained = owner_queue.Drain();

  bool ok = submitted && post_failures.load() == 0 && drained == kTaskCount &&
            !wrong_thread;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const int expected = CpuWork(static_cast<int>(i));
    ok = ok && results.at(i) == expected;
    std::cout << "owner-thread task " << i << " -> " << results.at(i) << '\n';
  }
  return ok;
}

}  // namespace

int main() {
  std::cout << "SocketWire ThreadPool demo\n\n";

  std::cout << "Low-level ThreadPool::Submit\n";
  const bool low_level_ok = LowLevelThreadPoolDemo();

  std::cout << "\nHigh-level ThreadPool + TaskQueue owner-thread pattern\n";
  const bool high_level_ok = HighLevelOwnerThreadDemo();

  if (!low_level_ok || !high_level_ok) {
    std::cerr << "thread-pool-demo failed self-check\n";
    return 1;
  }

  std::cout << "\nthread-pool-demo self-check passed\n";
  return 0;
}

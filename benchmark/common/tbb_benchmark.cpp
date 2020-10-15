#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include <exception>
#include <iostream>
#include <random>
#include <thread>
#include <algorithm>
#include <numeric>

#include "common/worker_pool.h"

namespace terrier {

class TBBBENCHMARK : public benchmark::Fixture {};

static uint64_t sum_restricted(const uint8_t *__restrict__ a, uint64_t start_index, uint64_t end_index) {
  uint64_t total = 0;

  for (uint64_t i = start_index; i < end_index; i++)
    total += static_cast<uint64_t>(a[i]);

  return total;
}

//static uint64_t sum_vectorized(const uint8_t * a, uint64_t start_index, uint64_t end_index) {
//  uint64_t total = 0;
//
//  uint64_t i;
//#pragma ivdep
//  for (i = start_index; i < end_index; i++)
//    total += static_cast<uint64_t>(a[i]);
//
//  return total;
//}
//
//static uint64_t sum_both(const uint8_t * __restrict__ a, uint64_t start_index, uint64_t end_index) {
//  uint64_t total = 0;
//
//  uint64_t i;
//#pragma ivdep
//  for (i = start_index; i < end_index; i++)
//    total += static_cast<uint64_t>(a[i]);
//
//  return total;
//}

BENCHMARK_DEFINE_F(TBBBENCHMARK, TBBBasicNoAllocation)(benchmark::State &state) {
  // 500 MB array.
  std::vector<uint8_t> array(state.range(1));

  // Fill with garbage.
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<uint8_t> dist;
  std::generate(array.begin(), array.end(), [&]() { return dist(gen); });

  // Num threads from arguments.
  const uint32_t num_threads = state.range(0);

  for (auto _ : state) {
    // Create thread pool.
    tbb::task_arena arena(num_threads);

    // Launch.
    uint64_t sum;
    arena.execute([&] {
      sum = tbb::parallel_reduce(
          tbb::blocked_range<std::vector<uint8_t>::const_iterator>(array.begin(), array.end()),
          // Identity element
          uint64_t(0),
          // Reduce a subrange and partial sum
          [&](const auto &range, uint64_t partial_sum) -> uint64_t {
            return std::accumulate(range.begin(), range.end(), partial_sum);
          },
          // Reduce two partial sums
          std::plus<>());
    });
    benchmark::DoNotOptimize(sum);
  }
}

BENCHMARK_DEFINE_F(TBBBENCHMARK, WorkerPoolBasicNoAllocation)(benchmark::State &state) {
  std::vector<uint8_t> array(state.range(1));

  // Fill with garbage.
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<uint8_t> dist;
  std::generate(array.begin(), array.end(), [&]() { return dist(gen); });

  // Num threads from arguments.
  const uint32_t num_threads = state.range(0);
  common::TaskQueue queue;
  std::atomic<uint64_t> total = 0;
  for (uint64_t thread_id = 0; thread_id < static_cast<uint64_t>(num_threads); thread_id++) {
    queue.emplace([&, thread_id] {
      uint64_t start_index = (array.size() / num_threads) * thread_id;
      uint64_t end_index = (array.size() / num_threads) * (thread_id + 1);

      tbb::task_arena arena(1);
      uint64_t local_total;
      arena.execute([&, start_index, end_index] {
        auto range = tbb::blocked_range<std::vector<uint8_t>::const_iterator>(array.begin() + start_index, array.begin() + end_index);
        local_total = tbb::parallel_reduce(
            range,
            // Identity element
            uint64_t(0),
            // Reduce a subrange and partial sum
            [&](const auto &range, uint64_t partial_sum) -> uint64_t {
              return std::accumulate(range.begin(), range.end(), partial_sum);
            },
            // Reduce two partial sums
            std::plus<>());
      });

      total += local_total;
    });
  }

  for (auto _ : state) {
    // Create thread pool.
    common::WorkerPool pool(num_threads, queue);

    pool.Startup();
    pool.WaitUntilAllFinished();
  }
}

BENCHMARK_DEFINE_F(TBBBENCHMARK, VectorizationInWorkerPoolRestricted)(benchmark::State &state) {
  uint64_t size = static_cast<uint64_t>(state.range(1));
  std::vector<uint8_t> array(size);

  // Fill with garbage.
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<uint8_t> dist;
  std::generate(array.begin(), array.end(), [&]() { return dist(gen); });

  // Num threads from arguments.
  const uint32_t num_threads = state.range(0);

  std::string x;
  std::cin >> x;

  for (auto _ : state) {
    // Create thread pool.
    std::atomic<uint64_t> total = 0;
    common::WorkerPool pool(num_threads, {});
    pool.Startup();

    for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++)
      pool.SubmitTask([&, thread_id] {
        uint64_t start_index = (array.size() / num_threads) * thread_id;
        uint64_t end_index = (array.size() / num_threads) * (thread_id + 1);
        total += sum_restricted(array.data(), start_index, end_index);
      });

    pool.WaitUntilAllFinished();
  }
}

//BENCHMARK_DEFINE_F(TBBBENCHMARK, VectorizationInWorkerPoolBoth)(benchmark::State &state) {
//  uint64_t size = static_cast<uint64_t>(state.range(1));
//  std::vector<uint8_t> array(size);
//
//  // Fill with garbage.
//  std::mt19937 gen(std::random_device{}());
//  std::uniform_int_distribution<uint8_t> dist;
//  std::generate(array.begin(), array.end(), [&]() { return dist(gen); });
//
//  // Num threads from arguments.
//  const uint32_t num_threads = state.range(0);
//
//  for (auto _ : state) {
//    // Create thread pool.
//    std::atomic<uint64_t> total = 0;
//    common::WorkerPool pool(num_threads, {});
//    pool.Startup();
//
//    for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++)
//      pool.SubmitTask([&, thread_id] {
//        uint64_t start_index = (array.size() / num_threads) * thread_id;
//        uint64_t end_index = (array.size() / num_threads) * (thread_id + 1);
//        total += sum_both(array.data(), start_index, end_index);
//      });
//
//    pool.WaitUntilAllFinished();
//  }
//}

namespace {

  static void CustomArguments(benchmark::internal::Benchmark *b) {
    int64_t sizes[] = {
      500 * 1024 * 1024,
//      1000 * 1024 * 1024,
//      5000UL * 1024 * 1024,
//      10000UL * 1024 * 1024,
//      50000UL * 1024 * 1024,
    };
    for (auto &size : sizes) {
      for (int64_t num_threads = std::thread::hardware_concurrency(); num_threads <= std::thread::hardware_concurrency(); num_threads++) {
        b->Args({num_threads, size});
      }
    }
  }

}  // namespace


//BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasicNoAllocation)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasicNoAllocation)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(TBBBENCHMARK, VectorizationInWorkerPoolRestricted)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, VectorizationInWorkerPoolSTDReduce)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);
}

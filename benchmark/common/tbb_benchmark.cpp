#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include <exception>
#include <iostream>
#include <random>
#include <thread>

#include "common/scoped_timer.h"
#include "common/worker_pool.h"

namespace terrier {

class TBBBENCHMARK : public benchmark::Fixture {};

struct ParallelRunner
{
  ParallelRunner(char* array) : array_(array) {}
  ParallelRunner(ParallelRunner &p, tbb::split) : array_(p.array_) {}
  void operator()(const tbb::blocked_range<uint64_t>& range) const
  {
    char *output = new char[range.end() - range.begin()];
    for (uint64_t i = range.begin(); i < range.end(); ++i)
      output[i - range.begin()] = array_[i];

    delete []output;
  }

  char* array_;
};

BENCHMARK_DEFINE_F(TBBBENCHMARK, TBBBasic)(benchmark::State &state){
  std::vector<uint64_t> sizes;
  uint64_t min_size = 1024 * 1024 * 1024;
  uint64_t max_size = 64UL * 1024UL * 1024UL * 1024UL;
//  uint64_t max_size = 5 * min_size;
  uint64_t num_sizes = 10;
  uint64_t size_change = (max_size - min_size) / num_sizes;
  for (uint64_t size = min_size; size <= max_size; size += size_change)
    sizes.push_back(size);

  char *array = new char[sizes[sizes.size() - 1]];
  for (uint64_t i = 0; i < sizes[sizes.size() - 1]; i++)
    array[i] = static_cast<char>(i % sizeof(char));

  for (auto _ : state) {
    for (int num_threads = 1; num_threads <= static_cast<int>(std::thread::hardware_concurrency()); num_threads += 3) {
      for (uint64_t size_index = 0; size_index < sizes.size(); size_index++) {
        try {
          uint64_t size = sizes[size_index];
          uint64_t time;
          {
            common::ScopedTimer<std::chrono::milliseconds> timer(&time);
            tbb::task_arena arena(num_threads);

            ParallelRunner runner(array);
            arena.execute([&] {
              tbb::parallel_for(tbb::blocked_range<uint64_t>(0, size), runner);
            });
          }

          std::cout << num_threads << ", " << size << ", " << time << std::endl;

        } catch (std::exception &e) {}

      }
    }
  }

  delete [] array;
}

BENCHMARK_DEFINE_F(TBBBENCHMARK, WorkerPoolBasic)(benchmark::State &state){
  std::vector<uint64_t> sizes;
  uint64_t min_size = 1024 * 1024 * 1024;
  uint64_t max_size = 64UL * 1024UL * 1024UL * 1024UL;
//  uint64_t max_size = 5 * min_size;
  uint64_t num_sizes = 10;
  uint64_t size_change = (max_size - min_size) / num_sizes;
  for (uint64_t size = min_size; size <= max_size; size += size_change)
    sizes.push_back(size);

  char *array = new char[sizes[sizes.size() - 1]];
  for (uint64_t i = 0; i < sizes[sizes.size() - 1]; i++)
    array[i] = static_cast<char>(i % sizeof(char));

  for (auto _ : state) {
    for (int num_threads = 1; num_threads <= static_cast<int>(std::thread::hardware_concurrency()); num_threads += 3) {
      for (uint64_t size_index = 0; size_index < sizes.size(); size_index++) {
        try {
          uint64_t size = sizes[size_index];
          uint64_t time;
          common::TaskQueue queue;
          for (uint64_t thread_id = 0; thread_id < static_cast<uint64_t>(num_threads); thread_id++) {
            queue.emplace([&, thread_id] {
              uint64_t start_index = (size / num_threads) * thread_id;
              uint64_t end_index = (size / num_threads) * (thread_id + 1);
              char *output = new char[end_index - start_index];

              for (uint64_t i = start_index; i < end_index; i++) output[i - start_index] = array[i];

              delete[] output;
            });
          }

          common::WorkerPool thread_pool(num_threads, queue);

          {
            common::ScopedTimer<std::chrono::milliseconds> timer(&time);
            thread_pool.Startup();
            thread_pool.WaitUntilAllFinished();
          }

          std::cout << num_threads << ", " << size << ", " << time << std::endl;

        } catch (std::exception &e) {
        }
      }
    }
  }

  delete [] array;

}

BENCHMARK_DEFINE_F(TBBBENCHMARK, TBBBasicNoAllocation)(benchmark::State &state) {
  // 500 MB array.
  std::vector<uint8_t> array(500 * 1024 * 1024);

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
    uint64_t sum = arena.execute([&] {
      return tbb::parallel_reduce(
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

namespace {

  static void CustomArguments(benchmark::internal::Benchmark *b) {
    for (int64_t num_threads = 1; num_threads <= std::thread::hardware_concurrency(); num_threads++) {
      b->Args({num_threads});
    }
  }

}  // namespace


//BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasicNoAllocation)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);

}

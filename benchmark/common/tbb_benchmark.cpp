#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include <exception>
#include <iostream>
#include <random>
#include <thread>

namespace terrier {

class TBBBENCHMARK : public benchmark::Fixture {};

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

namespace {

  static void CustomArguments(benchmark::internal::Benchmark *b) {
    int64_t sizes[] = {
      500 * 1024 * 1024,
      1000 * 1024 * 1024,
      5000UL * 1024 * 1024,
      10000UL * 1024 * 1024,
      50000UL * 1024 * 1024,
    };
    for (auto &size : sizes) {
      for (int64_t num_threads = 1; num_threads <= std::thread::hardware_concurrency(); num_threads++) {
        b->Args({num_threads, size});
      }
    }
  }

}  // namespace


//BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasic)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasicNoAllocation)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasicNoAllocation)->Apply(CustomArguments)->Iterations(50)->Unit(benchmark::kMillisecond);

}

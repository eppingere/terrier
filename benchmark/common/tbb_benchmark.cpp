#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <exception>
#include <iostream>
#include <thread>

#include "common/scoped_timer.h"

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

BENCHMARK_DEFINE_F(TBBBENCHMARK, Basic)(benchmark::State &state){


  std::vector<uint64_t> sizes;
  uint64_t min_size = 1024 * 1024 * 1024;
  uint64_t max_size = 64UL * 1024UL * 1024UL * 1024UL;
  uint64_t num_sizes = 10;
  uint64_t size_change = (max_size - min_size) / num_sizes;
  for (uint64_t size = min_size; size <= max_size; size += size_change)
    sizes.push_back(size);

  for (int num_threads = 1; num_threads <= std::thread::hardware_concurrency(); num_threads++) {
    for (auto &size : sizes) {
      try {
        char* array = new char [size];

        for (uint64_t i = 0; i < size; i++) array[i] = i;

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

        delete[] array;
      } catch (std::exception &e) {}

    }
  }

}
BENCHMARK_REGISTER_F(TBBBENCHMARK, Basic);

}

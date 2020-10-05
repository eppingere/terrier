#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <exception>
#include <iostream>
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
//  uint64_t max_size = 64UL * 1024UL * 1024UL * 1024UL;
  uint64_t max_size = 5 * min_size;
  uint64_t num_sizes = 10;
  uint64_t size_change = (max_size - min_size) / num_sizes;
  for (uint64_t size = min_size; size <= max_size; size += size_change)
    sizes.push_back(size);

  std::vector<char*> arrays;
  for (auto &size : sizes) {
    auto array = new char [size];
    for (uint64_t i = 0; i < size; i++)
      array[i] = i;
    arrays.push_back(array);
  }

  for (int num_threads = 1; num_threads <= static_cast<int>(std::thread::hardware_concurrency()); num_threads++) {
    for (uint64_t size_index = 0; size_index < arrays.size(); size_index++) {
      try {
        char *array = arrays[size_index];
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

  for (auto &array : arrays) delete [] array;

}

BENCHMARK_DEFINE_F(TBBBENCHMARK, WorkerPoolBasic)(benchmark::State &state){
  std::vector<uint64_t> sizes;
  uint64_t min_size = 1024 * 1024 * 1024;
//  uint64_t max_size = 64UL * 1024UL * 1024UL * 1024UL;
  uint64_t max_size = 5 * min_size;
  uint64_t num_sizes = 10;
  uint64_t size_change = (max_size - min_size) / num_sizes;
  for (uint64_t size = min_size; size <= max_size; size += size_change)
    sizes.push_back(size);

  std::vector<char*> arrays;
  for (auto &size : sizes) {
    auto array = new char [size];
    for (uint64_t i = 0; i < size; i++)
      array[i] = i;
    arrays.push_back(array);
  }

  for (int num_threads = 1; num_threads <= static_cast<int>(std::thread::hardware_concurrency()); num_threads++) {
    for (uint64_t size_index = 0; size_index < arrays.size(); size_index++) {
      try {
        char *array = arrays[size_index];
        uint64_t size = sizes[size_index];
        uint64_t time;
        common::TaskQueue queue;
        for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++) {
          queue.emplace([&, thread_id] {
            uint64_t start_index = (size / num_threads) * thread_id;
            uint64_t end_index = (size / num_threads) * (thread_id + 1);
            char *output = new char[end_index - start_index];

            for (uint64_t i = start_index; i < end_index; i++)
              output[i - start_index] = array[i];

            delete []output;
          });
        }

        common::WorkerPool thread_pool(num_threads, queue);

        {
          common::ScopedTimer<std::chrono::milliseconds> timer(&time);
          thread_pool.Startup();
          thread_pool.WaitUntilAllFinished();
        }

        std::cout << num_threads << ", " << size << ", " << time << std::endl;

      } catch (std::exception &e) {}

    }
  }

  for (auto &array : arrays) delete [] array;

}
BENCHMARK_REGISTER_F(TBBBENCHMARK, TBBBasic);
BENCHMARK_REGISTER_F(TBBBENCHMARK, WorkerPoolBasic);

}

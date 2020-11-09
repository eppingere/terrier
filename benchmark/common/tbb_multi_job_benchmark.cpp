#include <gbenchmark_ep/src/gbenchmark_ep-install/include/benchmark/benchmark.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>
#include <tbb/task_scheduler_init.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>

#include "common/worker_pool.h"

namespace terrier {

class TBBMULTIJOBBENCHMARK : public benchmark::Fixture {};

std::vector<uint8_t> parallel_load(uint64_t size) {
  std::vector<uint8_t> output(size);
  common::TaskQueue queue;
  for (uint64_t thread_id = 0; thread_id < std::thread::hardware_concurrency(); thread_id++) {
    queue.emplace([&, thread_id] {
      uint64_t start_index = (size/ std::thread::hardware_concurrency()) * thread_id;
      uint64_t end_index = (size / std::thread::hardware_concurrency()) * (thread_id + 1);
      for (uint64_t i = start_index; i < end_index; i++)
        output[i] = static_cast<uint8_t>(i % 256);
    });
  }

  common::WorkerPool pool(std::thread::hardware_concurrency(), queue);
  pool.Startup();
  pool.WaitUntilAllFinished();

  return output;
}

static uint64_t sum_restricted(const uint8_t *__restrict__ a, uint64_t start_index, uint64_t end_index) {
  uint64_t total = 0;

  for (uint64_t i = start_index; i < end_index; i++)
    total += static_cast<uint64_t>(a[i]);

  return total;
}

BENCHMARK_DEFINE_F(TBBMULTIJOBBENCHMARK, THREADPOOLBENCHMARK)(benchmark::State &state) {
  const uint32_t num_threads = state.range(0);
  uint64_t size = static_cast<uint64_t>(state.range(1));
  uint64_t num_jobs = static_cast<uint64_t>(state.range(2));

  std::vector<std::vector<uint8_t>> arrays;
  for (uint64_t i = 0; i < num_jobs; i++)
    arrays.emplace_back(parallel_load(size));

  std::string x;
  std::cin >> x;

  for (auto _ : state) {
    // Create thread pool.
    std::atomic<uint64_t>* num_done = new std::atomic<uint64_t>[num_jobs];
    std::vector<uint64_t> num_threads_per_job(num_jobs);
    std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>> end_time(num_jobs);
    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      num_done[job_num] = 0;
      num_threads_per_job[job_num] = 0;
    }

    for (uint64_t i = 0; i < num_threads; i++) {
      num_threads_per_job[i % num_jobs]++;
    }

    common::TaskQueue queue;


    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      uint64_t num_threads_for_job = std::max<uint64_t>(1, num_threads_per_job[job_num]);
      for (uint64_t thread_id = 0; thread_id < num_threads_for_job; thread_id++)
        queue.push([&, thread_id, job_num, num_threads_for_job] {
          uint64_t start_index = (arrays[job_num].size() / num_threads_for_job) * thread_id;
          uint64_t end_index = (arrays[job_num].size() / num_threads_for_job) * (thread_id + 1);
          uint64_t sum = sum_restricted(arrays[job_num].data(), start_index, end_index);
          benchmark::DoNotOptimize(sum);
          uint64_t now_done = ++num_done[job_num];
          if (now_done == num_threads_for_job) {
            end_time[job_num] = std::chrono::high_resolution_clock::now();
          }
        });
    }

//    std::cout << "num jobs: " << num_jobs << std::endl;
//    std::cout << "queue size: " << queue.size() << std::endl;
    TERRIER_ASSERT(queue.size() >= num_threads, "there should be as many jobs as threads");
    auto start_time = std::chrono::high_resolution_clock::now();
    common::WorkerPool pool(num_threads, queue);
    pool.Startup();
    pool.WaitUntilAllFinished();

    uint64_t total_ns = 0;
    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end_time[job_num] - start_time).count();
    }

    double total_ms_double = static_cast<double>(total_ns);
    double total_jobs_double = static_cast<double>(num_jobs);

    state.SetIterationTime((total_ms_double / total_jobs_double) / 1000000000.0);

    std::cerr << num_threads << ", " << num_jobs << ", " << (total_ms_double / total_jobs_double) / 1000000.0 << std::endl;

    delete []num_done;

  }
}

BENCHMARK_DEFINE_F(TBBMULTIJOBBENCHMARK, TBBBENCHMARK)(benchmark::State &state) {
  const uint32_t num_threads = state.range(0);
  uint64_t size = static_cast<uint64_t>(state.range(1));
  uint64_t num_jobs = static_cast<uint64_t>(state.range(2));

  std::vector<std::vector<uint8_t>> arrays;
  for (uint64_t i = 0; i < num_jobs; i++)
    arrays.emplace_back(parallel_load(size));

  std::vector<uint64_t> num_threads_per_job(num_jobs);
  std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>> end_time(num_jobs);
  for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
    num_threads_per_job[job_num] = 0;
  }

  for (uint64_t i = 0; i < num_threads; i++) {
    num_threads_per_job[i % num_jobs]++;
  }

  for (auto _ : state) {
    // Create thread pool.
    std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>> end_time(num_jobs);
    common::TaskQueue queue;


    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      auto range = tbb::blocked_range<std::vector<uint8_t>::const_iterator>(arrays[job_num].begin(), arrays[job_num].end());
      queue.emplace([&, job_num] {
        tbb::task_arena arena(std::max<int>(num_threads_per_job[job_num], 1));
        arena.execute([&, job_num] {
          tbb::parallel_reduce(
              range, uint64_t(0),
              [&](const auto &range, uint64_t partial_sum) -> uint64_t {
                return std::accumulate(range.begin(), range.end(), partial_sum);
              },
              std::plus<>());

          end_time[job_num] = std::chrono::high_resolution_clock::now();
        });
      });

    }

    auto start_time = std::chrono::high_resolution_clock::now();

    common::WorkerPool pool(num_threads, queue);
    pool.Startup();
    pool.WaitUntilAllFinished();

    uint64_t total_ns = 0;
    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end_time[job_num] - start_time).count();
    }

    double total_ms_double = static_cast<double>(total_ns);
    double total_jobs_double = static_cast<double>(num_jobs);

    state.SetIterationTime((total_ms_double / total_jobs_double) / 1000000000.0);

    std::cerr << num_threads << ", " << num_jobs << ", " << (total_ms_double / total_jobs_double) / 1000000.0 << std::endl;
  }
}

namespace {

  static void CustomArguments(benchmark::internal::Benchmark *b) {
    int64_t size = 100 * 1024 * 1024;

    std::vector<int64_t> job_nums = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20,
//        25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80,
    };

    std::vector<int64_t> thread_nums = {
        1, 2, 3, 4,// 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20,
//        25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80,
    };

//    for (int64_t num_threads = 1; num_threads <= std::thread::hardware_concurrency(); num_threads++) {
//      for (int64_t num_jobs = 1; num_jobs <= std::thread::hardware_concurrency(); num_jobs++) {
//        if ((num_threads / num_jobs) * num_jobs == num_threads)
//          b->Args({num_threads, size, num_jobs});
//      }
//    }

//    for (auto & num_jobs : job_nums) {
//      for (auto & num_threads : thread_nums) {
//        b->Args({num_threads, size, num_jobs});
//      }
//    }

    b->Args({80, size, 80});
  }

}  // namespace

BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, THREADPOOLBENCHMARK)->Apply(CustomArguments)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, TBBBENCHMARK)->Apply(CustomArguments)->Iterations(5)->Unit(benchmark::kMillisecond);

}

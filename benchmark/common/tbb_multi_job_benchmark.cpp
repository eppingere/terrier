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
#include "common/numa_worker_pool.h"

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

//  std::string x;
//  std::cin >> x;

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

static std::vector<uint8_t *> arrays_;

BENCHMARK_DEFINE_F(TBBMULTIJOBBENCHMARK, NUMATHREADPOOLBENCHMARK)(benchmark::State &state) {
  const uint32_t num_threads = state.range(0);
  uint64_t size = static_cast<uint64_t>(state.range(1));
  uint64_t num_jobs = static_cast<uint64_t>(state.range(2));

  if (arrays_.size() != std::thread::hardware_concurrency()) {
    for (uint64_t i = 0; i < std::thread::hardware_concurrency(); i++) {
      int region = i % common::num_numa_nodes();
      uint8_t *a;
#ifdef __APPLE__
      a = new uint8_t[size];
#else
      if (numa_available() == -1) {
        a = new uint8_t[size];
      } else {
        a = static_cast<uint8_t *>(numa_alloc_onnode(size, region));
      }
#endif
      for (uint64_t j = 0; j < size; j++) {
        a[j] = static_cast<uint8_t>(j);
      }
      arrays_.push_back(a);
    }
  }


//  std::string x;
//  std::cin >> x;

  common::NumaWorkerPool pool(num_threads, {});

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

    common::NumaWorkerPool::NumaTaskQueue queue;


    for (uint64_t job_num = 0; job_num < num_jobs; job_num++) {
      uint64_t num_threads_for_job = std::max<uint64_t>(1, num_threads_per_job[job_num]);
      for (uint64_t thread_id = 0; thread_id < num_threads_for_job; thread_id++)
        queue.push_back({[&, thread_id, job_num, num_threads_for_job] {
          uint64_t start_index = (size / num_threads_for_job) * thread_id;
          uint64_t end_index = (size / num_threads_for_job) * (thread_id + 1);
          uint64_t sum = sum_restricted(arrays_[job_num], start_index, end_index);
          benchmark::DoNotOptimize(sum);
          uint64_t now_done = ++num_done[job_num];
          if (now_done == num_threads_for_job) {
            end_time[job_num] = std::chrono::high_resolution_clock::now();
          }
        }, job_num % common::num_numa_nodes()});
    }

    TERRIER_ASSERT(queue.size() >= num_threads, "there should be as many jobs as threads");
    auto start_time = std::chrono::high_resolution_clock::now();
    for (auto &p : queue) pool.SubmitTask(p.first, p.second);
    pool.WaitTillFinished();

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

BENCHMARK_DEFINE_F(TBBMULTIJOBBENCHMARK, NUMATHREADPOOLLATENCYBENCHMARK)(benchmark::State &state) {
  const uint32_t NUM_TABLES = 5;
  const uint32_t NUM_THREADS = std::thread::hardware_concurrency();
  const uint32_t NUM_CLIENTS = 2000;

  std::default_random_engine generator;
  uint64_t average_size = (1024 * 1024 * 1024 * std::thread::hardware_concurrency()) / NUM_TABLES;
  std::normal_distribution<double> distribution(average_size,average_size / 5);

  common::NumaWorkerPool pool(NUM_THREADS, {});

  std::vector<uint8_t> tables[NUM_TABLES];
  for (uint32_t i = 0; i < NUM_TABLES; i++) {
    uint64_t i_size = static_cast<uint64_t>(std::max<double>(0.0, distribution(generator)));
    pool.SubmitTask([&, i, i_size] {
      tables[i] = std::vector<uint8_t>(i_size);
      for (uint64_t idx = 0; idx < i_size; idx++) {
        tables[i][idx] = static_cast<uint8_t>(idx);
      }
    }, i % common::num_numa_nodes());
  }

  pool.WaitTillFinished();

  std::atomic_bool done = false;

  std::vector<std::thread> threads;
  std::vector<double> ms_waited(NUM_CLIENTS);
  std::vector<uint64_t> num_items(NUM_CLIENTS);
  std::vector<uint64_t> num_jobs(NUM_CLIENTS);
  for (auto &i : ms_waited) i = 0.0;
  for (auto &i : num_items) i = 0;
  for (auto &i : num_jobs) i = 0;

  for (uint32_t i = 0; i < NUM_CLIENTS; i++) {
    threads.emplace_back([&, i] {
      std::default_random_engine gen;
      std::uniform_int_distribution<uint64_t> table_distro(0, NUM_TABLES - 1);

      while (!done) {
        uint64_t table_i = table_distro(gen);
        std::condition_variable done_cv;
        std::mutex l;
        std::unique_lock<std::mutex> lock(l);

        auto start_time = std::chrono::high_resolution_clock::now();
        pool.SubmitTask([&] {
          sum_restricted(tables[table_i].data(), 0, tables[table_i].size());

          std::unique_lock<std::mutex> lock(l);
          done_cv.notify_all();
        }, table_i % common::num_numa_nodes());

        done_cv.wait(lock);

        auto end_time = std::chrono::high_resolution_clock::now();
        ms_waited[i] += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        num_items[i] += tables[table_i].size();
        num_jobs[i]++;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

    });
  }

  std::this_thread::sleep_for(std::chrono::minutes(5));

  done = true;

  for (auto &thread : threads) thread.join();

  pool.WaitTillFinished();

  uint64_t items = 0;
  double total_ms = 0.0;
  for (uint64_t i = 0; i < NUM_CLIENTS; i++) {
    items += num_jobs[i];
    total_ms += ms_waited[i];
  }

  std::cerr << items << ", " << total_ms << std::endl;

}

BENCHMARK_DEFINE_F(TBBMULTIJOBBENCHMARK, THREADPOOLLATENCYBENCHMARK)(benchmark::State &state) {
  const uint32_t NUM_TABLES = 5;
  const uint32_t NUM_THREADS = std::thread::hardware_concurrency();
  const uint32_t NUM_CLIENTS = 500;

  std::default_random_engine generator;
  uint64_t average_size = (1024 * 1024 * 1024 * std::thread::hardware_concurrency()) / NUM_TABLES;
  std::normal_distribution<double> distribution(average_size,average_size / 5);

  common::WorkerPool pool(NUM_THREADS, {});

  std::vector<uint8_t> tables[NUM_TABLES];
  for (uint32_t i = 0; i < NUM_TABLES; i++) {
    uint64_t i_size = static_cast<uint64_t>(std::max<double>(0.0, distribution(generator)));
    pool.SubmitTask([&, i, i_size] {
      tables[i] = std::vector<uint8_t>(i_size);
      for (uint64_t idx = 0; idx < i_size; idx++) {
        tables[i][idx] = static_cast<uint8_t>(idx);
      }
    });
  }

  pool.WaitUntilAllFinished();

  std::atomic_bool done = false;

  std::vector<std::thread> threads;
  std::vector<double> ms_waited(NUM_CLIENTS);
  std::vector<uint64_t> num_items(NUM_CLIENTS);
  std::vector<uint64_t> num_jobs(NUM_CLIENTS);
  for (auto &i : ms_waited) i = 0.0;
  for (auto &i : num_items) i = 0;
  for (auto &i : num_jobs) i = 0;

  for (uint32_t i = 0; i < NUM_CLIENTS; i++) {
    threads.emplace_back([&, i] {
      std::default_random_engine gen;
      std::uniform_int_distribution<uint64_t> table_distro(0, NUM_TABLES - 1);

      while (!done) {
        uint64_t table_i = table_distro(gen);
        std::condition_variable done_cv;
        std::mutex l;
        std::unique_lock<std::mutex> lock(l);

        auto start_time = std::chrono::high_resolution_clock::now();
        pool.SubmitTask([&] {
          sum_restricted(tables[table_i].data(), 0, tables[table_i].size());

          std::unique_lock<std::mutex> lock(l);
          done_cv.notify_all();
        });

        done_cv.wait(lock);

        auto end_time = std::chrono::high_resolution_clock::now();
        ms_waited[i] += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        num_items[i] += tables[table_i].size();
        num_jobs[i]++;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

    });
  }

  std::this_thread::sleep_for(std::chrono::minutes(30));

  done = true;

  for (auto &thread : threads) thread.join();

  pool.WaitUntilAllFinished();

  uint64_t items = 0;
  double total_ms = 0.0;
  for (uint64_t i = 0; i < NUM_CLIENTS; i++) {
    items += num_jobs[i];
    total_ms += ms_waited[i];
  }

  std::cerr << items << ", " << total_ms << std::endl;

}

namespace {

  UNUSED_ATTRIBUTE static void CustomArguments(benchmark::internal::Benchmark *b) {
    int64_t size = 2000 * 1024 * 1024;

    std::vector<int64_t> job_nums = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20,
//        25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80,
    };

    std::vector<int64_t> thread_nums = {
        1, 2, 3, 4,// 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20,
//        25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80,
    };

    for (int64_t num_threads = 1; num_threads <= std::thread::hardware_concurrency(); num_threads++) {
//      for (int64_t num_jobs = 1; num_jobs <= std::thread::hardware_concurrency(); num_jobs++) {
//        if ((num_threads / num_jobs) * num_jobs == num_threads)
          b->Args({num_threads, size, num_threads});
//      }
    }

//    for (auto & num_jobs : job_nums) {
//      for (auto & num_threads : thread_nums) {
//        b->Args({num_threads, size, num_jobs});
//      }
//    }

//    b->Args({std::thread::hardware_concurrency(), size, std::thread::hardware_concurrency()});
  }

}  // namespace

BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, THREADPOOLBENCHMARK)->Apply(CustomArguments)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, NUMATHREADPOOLBENCHMARK)->Apply(CustomArguments)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, TBBBENCHMARK)->Apply(CustomArguments)->Iterations(5)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, NUMATHREADPOOLLATENCYBENCHMARK)->Iterations(1)->Unit(benchmark::kMillisecond);
//BENCHMARK_REGISTER_F(TBBMULTIJOBBENCHMARK, THREADPOOLLATENCYBENCHMARK)->Iterations(1)->Unit(benchmark::kMillisecond);

}

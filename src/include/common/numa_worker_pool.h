#pragma once
#include <atomic>
#include <condition_variable>  // NOLINT
#include <functional>
#include <mutex>  // NOLINT
#include <queue>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "common/macros.h"
#include "common/worker_pool.h"
#include "common/numa.h"

namespace terrier::common {

/**
 * A worker pool that maintains a group of worker threads and a task queue.
 *
 * As soon as there is a task in the task queue, a worker thread will be
 * assigned to run that task. A task must be a function that takes no argument.
 * After a worker finishes a task, it will eagerly try to get a new task.
 *
 * This pool is restartable, meaning it can be started again after it has been
 * shutdown. Calls to Startup() and Shutdown() are thread-safe.
 */
class NumaWorkerPool {
 public:

  using NumaTaskQueue = std::vector<std::pair<std::function<void()>, int>>;

  /**
   * Initialize the worker pool. You have to call StartUp if you want the threadpool to start.
   *
   * @param num_workers the number of workers in this pool
   * @param task_queue a queue of tasks
   */
  // NOLINTNEXTLINE  lint thinks it has only one arguement
  NumaWorkerPool(int num_workers, NumaTaskQueue task_queue)
      : workers_(num_numa_nodes()), is_running_(true), tasks_(num_numa_nodes()), busy_workers(0), num_workers_(num_workers) {

    for (auto &p : task_queue) {
      TERRIER_ASSERT(p.second < num_numa_nodes(), "must have valid numa node for task");
      tasks_[p.second].emplace(p.first);
    }

    for (int i = 0; i < num_workers_; i++) {
      int region = i % num_numa_nodes();
      workers_[region].push_back(std::thread([&, region] {
        set_thread_affinity(region);
        std::function<void()> task;

        while (true) {
          {
            // grab the lock
            std::unique_lock<std::mutex> lock(task_lock_);
            // task_cv_ is notified by new tasks and shutdown command, but lost notify can happen.
            task_cv_.wait(lock, [&] { return !is_running_ || !tasks_[region].empty(); });
            if (!is_running_) {
              // we are shutting down.
              return;
            }
            // Grab a new task
            task = std::move(tasks_[region].front());
            tasks_[region].pop();
            busy_workers++;
          }
          // We don't hold locks at this point
          task();
          {
            std::unique_lock<std::mutex> lock(task_lock_);
            busy_workers--;
          }
          // After completing the task, we only need to tell the master thread
          finished_cv_.notify_one();
        }
      }));
    }
  }

  /**
   * Destructor. Wake up all workers and let them finish before it's destroyed.
   */
  ~NumaWorkerPool() {
    std::unique_lock<std::mutex> lock(task_lock_);  // grab the lock
    is_running_ = false;                            // signal all the threads to shutdown
    task_cv_.notify_all();                          // wake up all the threads
    lock.unlock();                                  // free the lock
    for (auto &region : workers_)
      for (auto &thread : region)
        thread.join();
  }

  /**
   * It tells all worker threads to finish their current task and stop working.
   * No more tasks will be consumed. It waits until all worker threads stop working.
   */
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(task_lock_);
      is_running_ = false;
    }
    // tell everyone to stop working
    task_cv_.notify_all();
    for (auto &worker_region : workers_)
      for (auto &worker : worker_region)
        worker.join();
    workers_.clear();
  }

  void WaitTillFinished() {
    std::unique_lock<std::mutex> lock(task_lock_);
    finished_cv_.wait(lock, [&] {
      if (busy_workers != 0) return false;
      for (auto &q : tasks_)
        if (!q.empty())
          return false;
      return true;
    });
  }

  /**
   * Add a task to the task queue and inform worker threads.
   * You can only submit tasks after the thread pool has started up.
   *
   * @param func the new task
   */
  template <typename F>
  void SubmitTask(const F &func, int region) {
    TERRIER_ASSERT(is_running_, "Only allow to submit task after the thread pool has been started up");
    TERRIER_ASSERT(region < num_numa_nodes(), "must be valid region");
    {
      std::lock_guard<std::mutex> lock(task_lock_);
      tasks_[region].emplace(std::move(func));
    }
    task_cv_.notify_one();
  }

 private:
  // The worker threads
  std::vector<std::vector<std::thread>> workers_;
  // Flag indicating whether the pool is running
  bool is_running_;

  // The queue where workers pick up tasks
  std::vector<std::queue<std::function<void()>>> tasks_;

  uint32_t busy_workers, num_workers_;

  std::mutex task_lock_;

  std::condition_variable task_cv_;

  std::condition_variable finished_cv_;
};
}  // namespace terrier::common

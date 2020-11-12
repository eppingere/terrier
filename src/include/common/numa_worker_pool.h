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
  /**
   * Initialize the worker pool. You have to call StartUp if you want the threadpool to start.
   *
   * @param num_workers the number of workers in this pool
   * @param task_queue a queue of tasks
   */
  // NOLINTNEXTLINE  lint thinks it has only one arguement
  NumaWorkerPool(TaskQueue task_queue)
      : is_running_(false), task_queue_(std::move(task_queue)), busy_workers_{0}, num_workers_(std::thread::hardware_concurrency()) {
    // Walk it off, son. We have nothing else to do here...
  }

  /**
   * Destructor. Wake up all workers and let them finish before it's destroyed.
   */
  ~WorkerPool() {
    std::unique_lock<std::mutex> lock(task_lock_);  // grab the lock
    is_running_ = false;                            // signal all the threads to shutdown
    task_cv_.notify_all();                          // wake up all the threads
    lock.unlock();                                  // free the lock
    for (auto &thread : workers_) thread.join();
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
    for (auto &worker : workers_) {
      worker.join();
    }
    workers_.clear();
  }

  /**
   * Add a task to the task queue and inform worker threads.
   * You can only submit tasks after the thread pool has started up.
   *
   * @param func the new task
   */
  template <typename F>
  void SubmitTask(const F &func) {
    TERRIER_ASSERT(is_running_, "Only allow to submit task after the thread pool has been started up");
    {
      std::lock_guard<std::mutex> lock(task_lock_);
      task_queue_.emplace(std::move(func));
    }
    task_cv_.notify_one();
  }

  /**
   * Block until all the tasks in the task queue has been completed
   */
  void WaitUntilAllFinished() {
    std::unique_lock<std::mutex> lock(task_lock_);
    // wait for all the tasks to complete

    // If finished_cv_ is notified by worker threads. Lost notify can happen.
    finished_cv_.wait(lock, [&] { return busy_workers_ == 0 && task_queue_.empty(); });
  }

  /**
   * Change the number of worker threads. It can only be done when the thread pool
   * if not running.
   *
   * @param num the number of worker threads.
   */
  void SetNumWorkers(uint32_t num) {
    TERRIER_ASSERT(!is_running_, "Only allow to set num of workers when the thread pool is not running");
    num_workers_ = num;
  }

 private:
  // The worker threads
  std::vector<std::vector<std::thread>> workers_;
  // Flag indicating whether the pool is running
  bool is_running_;
  // The queue where workers pick up tasks
  TaskQueue task_queue_;

  uint32_t busy_workers_, num_workers_;

  std::mutex task_lock_;

  std::condition_variable task_cv_;

  std::condition_variable finished_cv_;
};
}  // namespace terrier::common

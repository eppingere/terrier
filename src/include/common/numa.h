#ifndef __APPLE__
#include <numa.h>
#include <numaif.h>
#endif
#include <map>
#include "macros.h"
#include <thread>
#include <sched.h>

namespace terrier::common {
  static int num_numa_nodes() {
#ifdef __APPLE__
    return 1;
#else
    return static_cast<int>((numa_available() >= 0 && numa_max_node() >= 0) ? (numa_max_node() + 1) : 1);
#endif
  }

  UNUSED_ATTRIBUTE static int region_of_cpu(int cpu) {
#ifdef __APPLE__
    return 0;
#else
    if (numa_available() < 0) return 0;
    int res = numa_node_of_cpu(cpu);
    return res < 0 ? 0 : res;
#endif
  }

  static std::map<int, int> num_pinned_threads_;
  static void set_thread_affinity(int region) {
    TERRIER_ASSERT(region < num_numa_nodes(), "must be valid region");
#ifndef __APPLE__
    int cpu_id = -1;
    for (uint32_t cpu = 0; cpu < std::thread::hardware_concurrency(); cpu++) {
      if (region_of_cpu(cpu) == region) {
        if (cpu_id == -1 || !num_pinned_threads_.count() ||
            num_pinned_threads_[cpu] < num_pinned_threads_[cpu_id]) {
          cpu_id = cpu;
        }
      }
    }
    TERRIER_ASSERT(cpu_id != -1, "should be at least 1 valid cpu");

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    int result UNUSED_ATTRIBUTE = sched_setaffinity(0, sizeof(cpu_set_t), &mask);

    TERRIER_ASSERT(result == 0, "sched_setaffinity should succeed");

    if (num_pinned_threads_.count(cpu_id)) {
      num_pinned_threads_[cpu_id]++;
    } else {
      num_pinned_threads_[cpu_id] = 1;
    }
#endif
  }

}
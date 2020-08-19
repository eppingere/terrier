#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "catalog/catalog_accessor.h"
#include "catalog/catalog_defs.h"
#include "common/managed_pointer.h"
#include "execution/compiler/executable_query.h"
#include "execution/exec/execution_settings.h"
#include "execution/table_generator/sample_output.h"
#include "execution/vm/module.h"

namespace terrier::execution::exec {
class ExecutionContext;
}

namespace terrier::catalog {
class Catalog;
}

namespace terrier::transaction {
class TransactionManager;
}

namespace terrier {
class DBMain;
}

namespace terrier::tpch {

/**
 * Class that can load the TPCH tables, compile the TPCH queries, and execute TPCH workloads
 */
class Workload {
 public:
  Workload(common::ManagedPointer<DBMain> db_main, const std::string &db_name, const std::string &table_root);

  /**
   * Function to invoke for a single worker thread to invoke the TPCH queries
   * @param worker_id 1-indexed thread id
   * @param execution_us_per_worker max execution time for single worker
   * @param avg_interval_us interval timing
   * @param query_num number of queries to run
   * @param mode execution mode
   */
  void Execute(int8_t worker_id, uint64_t execution_us_per_worker, uint64_t avg_interval_us, uint32_t query_num,
               execution::vm::ExecutionMode mode);

  /**
   * Function to invoke a single TPCH query and collect runtime
   * @param query_ind index of query into query_and_plan_
   * @param avg_interval_us interval timing
   * @param mode execution mode
   * @return time taken to run query
   */
  uint64_t ExecuteQuery(int32_t query_ind, execution::vm::ExecutionMode mode);

  /**
   * Function to get number of queries in plan
   * @return size of query plan vector
   */
  uint32_t GetQueryNum() const { return query_and_plan_.size(); }

 private:
  void GenerateTPCHTables(execution::exec::ExecutionContext *exec_ctx, const std::string &dir_name);

  void LoadTPCHQueries(const std::unique_ptr<catalog::CatalogAccessor> &accessor);

  common::ManagedPointer<DBMain> db_main_;
  common::ManagedPointer<storage::BlockStore> block_store_;
  common::ManagedPointer<catalog::Catalog> catalog_;
  common::ManagedPointer<transaction::TransactionManager> txn_manager_;
  catalog::db_oid_t db_oid_;
  catalog::namespace_oid_t ns_oid_;
  execution::exec::ExecutionSettings exec_settings_{};
  std::unique_ptr<catalog::CatalogAccessor> accessor_;

  std::vector<
      std::tuple<std::unique_ptr<execution::compiler::ExecutableQuery>, std::unique_ptr<planner::AbstractPlanNode>>>
      query_and_plan_;
  std::vector<std::string> query_names_;
};

}  // namespace terrier::tpch

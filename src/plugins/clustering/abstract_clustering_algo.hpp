#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <utility>

#include "storage/chunk.hpp"
#include "storage/storage_manager.hpp"
#include "storage/table.hpp"

namespace opossum {

using ClusteringByTable = std::map<std::string, std::vector<std::pair<std::string, uint32_t>>>;
class AbstractClusteringAlgo {
 public:

  AbstractClusteringAlgo(ClusteringByTable clustering) : clustering_by_table(clustering) {}

  virtual ~AbstractClusteringAlgo() = default;

  virtual const std::string description() const = 0;

  void run();

  ClusteringByTable clustering_by_table;

 protected:
  void _run_assertions() const;
  virtual void _perform_clustering() = 0;

  // helper functions
  std::shared_ptr<Chunk> _create_empty_chunk(const std::shared_ptr<const Table>& table, const size_t rows_per_chunk) const;
  Segments _get_segments(const std::shared_ptr<const Chunk> chunk) const;
  void _append_chunk_to_table(const std::shared_ptr<Chunk> chunk, const std::shared_ptr<Table> table, bool allow_mutable=true) const;
  void _append_sorted_chunk_to_table(const std::shared_ptr<Chunk> chunk, const std::shared_ptr<Table> table, bool allow_mutable=true) const;
  void _append_chunks_to_table(const std::vector<std::shared_ptr<Chunk>>& chunks, const std::shared_ptr<Table> table, bool allow_mutable=true) const;
  void _append_sorted_chunks_to_table(const std::vector<std::shared_ptr<Chunk>>& chunks, const std::shared_ptr<Table> table, bool allow_mutable=true) const;
  std::shared_ptr<Chunk> _sort_chunk(std::shared_ptr<Chunk> chunk, const ColumnID sort_column, const TableColumnDefinitions& column_definitions) const;

  std::map<std::string, size_t> _original_table_sizes;
};

}  // namespace opossum

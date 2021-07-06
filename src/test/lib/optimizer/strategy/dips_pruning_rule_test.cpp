#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base_test.hpp"
#include "lib/optimizer/strategy/strategy_base_test.hpp"
#include "utils/assert.hpp"

#include "expression/expression_functional.hpp"
#include "hyrise.hpp"
#include "logical_query_plan/join_node.hpp"
#include "logical_query_plan/lqp_translator.hpp"
#include "logical_query_plan/mock_node.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/projection_node.hpp"
#include "logical_query_plan/sort_node.hpp"
#include "logical_query_plan/stored_table_node.hpp"
#include "logical_query_plan/union_node.hpp"
#include "logical_query_plan/validate_node.hpp"
#include "operators/get_table.hpp"
#include "optimizer/strategy/dips_pruning_rule.hpp"
#include "statistics/generate_pruning_statistics.hpp"
#include "storage/chunk.hpp"
#include "storage/chunk_encoder.hpp"
#include "storage/table.hpp"

using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

class DipsPruningRuleTest : public StrategyBaseTest {
 public:
  void SetUp() override {
    auto& storage_manager = Hyrise::get().storage_manager;

    auto int_float2_table = load_table("resources/test_data/tbl/int_float2.tbl", 2u);
    ChunkEncoder::encode_all_chunks(int_float2_table, SegmentEncodingSpec{EncodingType::Dictionary});
    storage_manager.add_table("int_float2", int_float2_table);

    auto int_float2_sorted_table = load_table("resources/test_data/tbl/int_float2_sorted.tbl", 2u);
    ChunkEncoder::encode_all_chunks(int_float2_sorted_table, SegmentEncodingSpec{EncodingType::Dictionary});
    storage_manager.add_table("int_float2_sorted", int_float2_sorted_table);

    auto int_float2_sorted_mixed_table = load_table("resources/test_data/tbl/int_float2_sorted_mixed.tbl", 2u);
    ChunkEncoder::encode_all_chunks(int_float2_sorted_mixed_table, SegmentEncodingSpec{EncodingType::Dictionary});
    storage_manager.add_table("int_float2_sorted_mixed", int_float2_sorted_mixed_table);

    rule = std::make_shared<DipsPruningRule>();
  }

  std::shared_ptr<DipsPruningRule> rule;
};

TEST_F(DipsPruningRuleTest, RangeIntersectionTest) {
  // int32_t

  std::pair<int32_t, int32_t> first_range(1, 2);
  std::pair<int32_t, int32_t> second_range(3, 4);

  bool result_1 = rule->_range_intersect<int32_t>(first_range, second_range);
  bool result_2 = rule->_range_intersect<int32_t>(second_range, first_range);

  EXPECT_FALSE(result_1);
  EXPECT_FALSE(result_2);

  first_range = std::pair<int32_t, int32_t>(1, 8);
  second_range = std::pair<int32_t, int32_t>(3, 6);

  result_1 = rule->_range_intersect<int32_t>(first_range, second_range);
  result_2 = rule->_range_intersect<int32_t>(second_range, first_range);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);

  first_range = std::pair<int32_t, int32_t>(1, 8);
  second_range = std::pair<int32_t, int32_t>(0, 1);

  result_1 = rule->_range_intersect<int32_t>(first_range, second_range);
  result_2 = rule->_range_intersect<int32_t>(second_range, first_range);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);

  // double

  std::pair<double, double> first_range_double(1.4, 2.3);
  std::pair<double, double> second_range_double(3.3, 4.5);

  result_1 = rule->_range_intersect<double>(first_range_double, second_range_double);
  result_2 = rule->_range_intersect<double>(second_range_double, first_range_double);

  EXPECT_FALSE(result_1);
  EXPECT_FALSE(result_2);

  first_range_double = std::pair<double, double>(2.1, 8.4);
  second_range_double = std::pair<double, double>(3.4, 6.9);

  result_1 = rule->_range_intersect<double>(first_range_double, second_range_double);
  result_2 = rule->_range_intersect<double>(second_range_double, first_range_double);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);

  first_range_double = std::pair<double, double>(1.0, 8.0);
  second_range_double = std::pair<double, double>(0.0, 1.0);

  result_1 = rule->_range_intersect<double>(first_range_double, second_range_double);
  result_2 = rule->_range_intersect<double>(second_range_double, first_range_double);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);

  // pmr_string

  std::pair<pmr_string, pmr_string> first_range_string("aa", "bb");
  std::pair<pmr_string, pmr_string> second_range_string("cc", "dd");

  result_1 = rule->_range_intersect<pmr_string>(first_range_string, second_range_string);
  result_2 = rule->_range_intersect<pmr_string>(second_range_string, first_range_string);

  EXPECT_FALSE(result_1);
  EXPECT_FALSE(result_2);

  first_range_string = std::pair<pmr_string, pmr_string>("aa", "gg");
  second_range_string = std::pair<pmr_string, pmr_string>("cc", "ee");

  result_1 = rule->_range_intersect<pmr_string>(first_range_string, second_range_string);
  result_2 = rule->_range_intersect<pmr_string>(second_range_string, first_range_string);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);

  first_range_string = std::pair<pmr_string, pmr_string>("cc", "ff");
  second_range_string = std::pair<pmr_string, pmr_string>("aa", "cc");

  result_1 = rule->_range_intersect<pmr_string>(first_range_string, second_range_string);
  result_2 = rule->_range_intersect<pmr_string>(second_range_string, first_range_string);

  EXPECT_TRUE(result_1);
  EXPECT_TRUE(result_2);
}

TEST_F(DipsPruningRuleTest, CalculatePrunedChunks) {
  std::map<ChunkID, std::vector<std::pair<int32_t, int32_t>>> base_ranges{
      {ChunkID{0}, std::vector{std::pair<int32_t, int32_t>(1, 5)}},
      {ChunkID{1}, std::vector{std::pair<int32_t, int32_t>(8, 10)}},
      {ChunkID{2}, std::vector{std::pair<int32_t, int32_t>(10, 12)}}};
  std::map<ChunkID, std::vector<std::pair<int32_t, int32_t>>> partner_ranges{
      {ChunkID{0}, std::vector{std::pair<int32_t, int32_t>(6, 7)}},  // raus
      {ChunkID{1}, std::vector{std::pair<int32_t, int32_t>(9, 11)}},
      {ChunkID{2}, std::vector{std::pair<int32_t, int32_t>(12, 16)}}};

  auto pruned_chunks = rule->_calculate_pruned_chunks<int32_t>(base_ranges, partner_ranges);
  std::set<ChunkID> expected_pruned_chunk_ids{ChunkID{0}};

  EXPECT_EQ(pruned_chunks.size(), 1);
  EXPECT_TRUE((pruned_chunks == expected_pruned_chunk_ids));
}

TEST_F(DipsPruningRuleTest, ApplyPruningSimple) {
  // LEFT -> RIGHT
  auto stored_table_node_1 = std::make_shared<StoredTableNode>("int_float2_sorted");
  auto stored_table_node_2 = std::make_shared<StoredTableNode>("int_float2");
  auto join_node = std::make_shared<JoinNode>(JoinMode::Inner, equals_(lqp_column_(stored_table_node_2, ColumnID{0}),
                                                                       lqp_column_(stored_table_node_1, ColumnID{0})));
  join_node->set_left_input(stored_table_node_1);
  join_node->set_right_input(stored_table_node_2);

  std::vector<ChunkID> pruned_chunk_ids{ChunkID{1}};
  stored_table_node_2->set_pruned_chunk_ids(std::vector<ChunkID>(pruned_chunk_ids.begin(), pruned_chunk_ids.end()));

  StrategyBaseTest::apply_rule(rule, join_node);

  std::vector<ChunkID> expected_pruned_ids_right{ChunkID{0}, ChunkID{2}, ChunkID{3}};

  EXPECT_EQ(stored_table_node_1->pruned_chunk_ids(), expected_pruned_ids_right);

  // RIGHT -> LEFT

  stored_table_node_2->set_pruned_chunk_ids(std::vector<ChunkID>());
  stored_table_node_1->set_pruned_chunk_ids(std::vector<ChunkID>{ChunkID{0}, ChunkID{2}, ChunkID{3}});

  join_node = std::make_shared<JoinNode>(JoinMode::Inner, equals_(lqp_column_(stored_table_node_1, ColumnID{0}),
                                                                  lqp_column_(stored_table_node_2, ColumnID{0})));

  join_node->set_left_input(stored_table_node_2);
  join_node->set_right_input(stored_table_node_1);

  StrategyBaseTest::apply_rule(rule, join_node);

  std::vector<ChunkID> expected_pruned_ids_left{ChunkID{1}};

  EXPECT_EQ(stored_table_node_2->pruned_chunk_ids(), expected_pruned_ids_left);
}

// NOLINTS introduced because of line length when commented out
// TEST_F(DipsPruningRuleTest, DipsJoinGraphTraversal) {
//   // [table1 <-> table2 <-> table3] cycle free structure
//   std::shared_ptr<StoredTableNode> table1 = std::make_shared<StoredTableNode>("int_float2");
//   std::shared_ptr<StoredTableNode> table2 = std::make_shared<StoredTableNode>("int_float2_sorted");
//   std::shared_ptr<StoredTableNode> table3 = std::make_shared<StoredTableNode>("int_float2_sorted_mixed");

//   std::vector<ChunkID> table1_pruned_chunk_ids{};
//   std::vector<ChunkID> table2_pruned_chunk_ids{ChunkID{0}};
//   std::vector<ChunkID> table3_pruned_chunk_ids{};

//   table1->set_pruned_chunk_ids(std::vector<ChunkID>(table1_pruned_chunk_ids.begin(), table1_pruned_chunk_ids.end())); // NOLINT
//   table2->set_pruned_chunk_ids(std::vector<ChunkID>(table2_pruned_chunk_ids.begin(), table2_pruned_chunk_ids.end())); // NOLINT
//   table3->set_pruned_chunk_ids(std::vector<ChunkID>(table3_pruned_chunk_ids.begin(), table3_pruned_chunk_ids.end())); // NOLINT

//   std::shared_ptr<DipsJoinGraph> join_graph = std::make_shared<DipsJoinGraph>();  // build dips join graph

//   std::shared_ptr<DipsJoinGraphNode> table1_node = join_graph->get_node_for_table(table1);
//   std::shared_ptr<DipsJoinGraphNode> table2_node = join_graph->get_node_for_table(table2);
//   std::shared_ptr<DipsJoinGraphNode> table3_node = join_graph->get_node_for_table(table3);

//   std::shared_ptr<DipsJoinGraphEdge> table1_to_table2_edge =
//       table1_node->get_edge_for_table(table2_node);  // set int_float2 JOIN int_float2_sorted ON a=a
//   std::shared_ptr<DipsJoinGraphEdge> table2_to_table1_edge = table2_node->get_edge_for_table(table1_node);

//   table1_to_table2_edge->append_predicate(equals_(lqp_column_(table1, ColumnID{0}), lqp_column_(table2, ColumnID{0}))); // NOLINT
//   table2_to_table1_edge->append_predicate(equals_(lqp_column_(table1, ColumnID{0}), lqp_column_(table2, ColumnID{0}))); // NOLINT

//   std::shared_ptr<DipsJoinGraphEdge> table2_to_table3_edge =
//       table2_node->get_edge_for_table(table3_node);  // set int_float2 JOIN int_float2_sorted ON b=b
//   std::shared_ptr<DipsJoinGraphEdge> table3_to_table2_edge = table3_node->get_edge_for_table(table2_node);
//   table2_to_table3_edge->append_predicate(equals_(lqp_column_(table2, ColumnID{1}), lqp_column_(table3, ColumnID{1}))); // NOLINT
//   table3_to_table2_edge->append_predicate(equals_(lqp_column_(table2, ColumnID{1}), lqp_column_(table3, ColumnID{1}))); // NOLINT

//   EXPECT_TRUE(join_graph->is_tree());

//   join_graph->set_root(table1_node);  // prune based on dips
//   _rule->_bottom_up_dip_traversal(table1_node);

//   std::vector<ChunkID> expected_table1_pruned_ids{ChunkID{1}};
//   std::vector<ChunkID> expected_table2_pruned_ids{ChunkID{0}, ChunkID{2}, ChunkID{3}};
//   std::vector<ChunkID> expected_table3_pruned_ids{ChunkID{0}};

//   EXPECT_EQ(table1->pruned_chunk_ids(), expected_table1_pruned_ids);
//   EXPECT_EQ(table2->pruned_chunk_ids(), expected_table2_pruned_ids);
//   EXPECT_EQ(table3->pruned_chunk_ids(), expected_table3_pruned_ids);

//   _rule->_top_down_dip_traversal(table1_node);

//   expected_table1_pruned_ids = std::vector<ChunkID>{ChunkID{1}};
//   expected_table2_pruned_ids = std::vector<ChunkID>{ChunkID{0}, ChunkID{2}, ChunkID{3}};
//   expected_table3_pruned_ids = std::vector<ChunkID>{ChunkID{0}, ChunkID{2}, ChunkID{3}};

//   EXPECT_EQ(table1->pruned_chunk_ids(), expected_table1_pruned_ids);
//   EXPECT_EQ(table2->pruned_chunk_ids(), expected_table2_pruned_ids);
//   EXPECT_EQ(table3->pruned_chunk_ids(), expected_table3_pruned_ids);
// }

}  // namespace opossum

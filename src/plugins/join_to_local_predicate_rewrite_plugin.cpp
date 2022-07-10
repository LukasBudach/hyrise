/*
 *  In this plugin, we implement one way for UCC dependency discovery in table columns.
 *  In general, sort and adjacent_find are used for duplicate detection. The sort is optimized for sorted sub-vectors which are merged as a whole.
 *  Early outs are exploited for dictionary segments.
 */

#include "join_to_local_predicate_rewrite_plugin.hpp"

#include "expression/binary_predicate_expression.hpp"
#include "expression/expression_utils.hpp"
#include "expression/value_expression.hpp"
#include "hyrise.hpp"
#include "logical_query_plan/aggregate_node.hpp"
#include "logical_query_plan/join_node.hpp"
#include "logical_query_plan/lqp_utils.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/stored_table_node.hpp"
#include "magic_enum.hpp"
#include "storage/table.hpp"
#include "utils/timer.hpp"
#include "utils/format_duration.hpp"

#include "resolve_type.hpp"

namespace opossum {

std::string JoinToLocalPredicateRewritePlugin::description() const {
    return "This is the Hyrise JoinToLocalPredicateRewritePlugin";
}

void JoinToLocalPredicateRewritePlugin::start() {
    auto t = Timer();
    // std::cout << "The Hyrise JoinToLocalPredicateRewritePlugin was started..." << std::endl;

    const auto ucc_candidates = *identify_ucc_candidates();

    std::cout << "  - UCC Candidates: " << ucc_candidates.size() << std::endl;

    for (const auto& candidate : ucc_candidates) {
        const auto table = Hyrise::get().storage_manager.get_table(candidate.table_name());
        const auto col_id = candidate.column_id();

        // std::cout << "Attempting Unique Constraing Validation for:" << std::endl;
        // std::cout << candidate.table_name() << " -- " << table->column_name(col_id) << std::endl;

        //std::cout << "Existing Key Constraints:" << std::endl;
        const auto& soft_key_constraints = table->soft_key_constraints();
        /*
        for (const auto& key_constraint: soft_key_constraints) {
            for (const auto column: key_constraint.columns()) {
                std::cout << table->column_name(column) << " ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
        */

        const auto chunk_count = table->chunk_count();

        // Skip already discovered unique constraints.
        if (std::any_of(begin(soft_key_constraints), end(soft_key_constraints), [&col_id](const auto key_constraint) {
            const auto& columns = key_constraint.columns();
            return columns.size() == 1 && columns.contains(col_id);
        })) {
            continue;
        }

        resolve_data_type(table->column_data_type(col_id), [&](const auto data_type_t) {
            using ColumnDataType = typename decltype(data_type_t)::type;

            // all_values collects the segment values from all chunks.
            auto all_values = std::make_unique<std::unordered_set<ColumnDataType>>();
            auto all_values_size = all_values->size();

            // We can use an early-out if we find a single dict segment that contains a duplicate.
            for (auto chunk_id = ChunkID{0}; chunk_id < chunk_count; chunk_id ++) {
                const auto& source_chunk = table->get_chunk(chunk_id);
                const auto& source_segment = source_chunk->get_segment(col_id);

                if (std::dynamic_pointer_cast<DictionarySegment<ColumnDataType>>(source_segment)) {
                    const auto& dict_segment = std::dynamic_pointer_cast<DictionarySegment<ColumnDataType>>(source_segment);
                    const auto& dict = dict_segment->dictionary();
                    const auto& attr_vector = dict_segment->attribute_vector();

                    if (dict->size() != attr_vector->size()) {
                        return;
                    }
                }
            }

            // If we reach here, we have to make a cross-segment duplicate check.
            for (auto chunk_id = ChunkID{0}; chunk_id < chunk_count; chunk_id ++) {
                const auto& source_chunk = table->get_chunk(chunk_id);
                const auto& source_segment = source_chunk->get_segment(col_id);

                if (std::dynamic_pointer_cast<ValueSegment<ColumnDataType>>(source_segment)) {
                    const auto& val_segment = std::dynamic_pointer_cast<ValueSegment<ColumnDataType>>(source_segment);
                    const auto& values = val_segment->values();

                    all_values->insert(begin(values), end(values));
                    if (all_values->size() == all_values_size + values.size()) {
                        all_values_size += values.size();
                    } else {
                        // If not all elements have been inserted, there must have occured a duplicate, so the UCC constraint is violated.
                        return;
                    }

                    // std::cout << values.size() << std::endl;
                    // std::cout << "----" << std::endl;
                } else if (std::dynamic_pointer_cast<DictionarySegment<ColumnDataType>>(source_segment)) {
                    const auto& dict_segment = std::dynamic_pointer_cast<DictionarySegment<ColumnDataType>>(source_segment);
                    const auto& dict = dict_segment->dictionary();
                    const auto& attr_vector = dict_segment->attribute_vector();
                    
                    all_values->insert(begin(*dict), end(*dict));
                    if (all_values->size() == all_values_size + dict->size()) {
                        all_values_size += dict->size();
                    } else {
                        // If not all elements have been inserted, there must have occured a duplicate, so the UCC constraint is violated.
                        return;
                    }

                    // std::cout << dict->size() << std::endl;
                    // std::cout << attr_vector->size() << std::endl;
                    // std::cout << "----" << std::en   dl;
                } else {
                    Fail("The given segment type is not supported for the discovery of UCCs.");
                }
            }

            // We save UCC constraints directly inside the table so they can be forwarded to nodes in a query plan.
            std::cout << "  - Validated UCC on: " << table->column_name(col_id) << std::endl;
            table->add_soft_key_constraint(TableKeyConstraint(std::unordered_set(std::initializer_list<ColumnID>{col_id}), KeyConstraintType::UNIQUE));

            // std::cout << std::endl;
        });
    }
    std::cout << "  - Time for UCC discovery: " << format_duration(t.lap()) << std::endl;
}

void JoinToLocalPredicateRewritePlugin::stop() {
    // std::cout << "The Hyrise JoinToLocalPredicateRewritePlugin was stopped..." << std::endl;
}

std::shared_ptr<std::vector<UCCCandidate>> JoinToLocalPredicateRewritePlugin::generate_valid_candidates(std::shared_ptr<AbstractLQPNode> root_node, std::shared_ptr<LQPColumnExpression> column_candidate) {
    auto candidates = std::make_shared<std::vector<UCCCandidate>>();

    if (!root_node) {
        // input node may already be nullptr in case we try to get right input of node with only one input
        return nullptr;
    } else {
        visit_lqp(root_node, [&](auto& node) {
            if (node->type != LQPNodeType::Predicate) {
                return LQPVisitation::VisitInputs;
            }

            // std::cout << "\t\t\t\t" << node->description() << std::endl;
            
            // when looking at predicate node, check whether the searched column is filtered in this predicate
            // -> if so, it is a valid UCC candidate; if not, still continue search
            const auto casted_node = std::static_pointer_cast<PredicateNode>(node);

            // first, ensure that we look at a binary predicate expression checking for equality (e.g., A==B)
            const auto predicate = std::dynamic_pointer_cast<BinaryPredicateExpression>(casted_node->predicate());
            if ((!predicate) || (predicate->predicate_condition != PredicateCondition::Equals)) {
                return LQPVisitation::VisitInputs;
            }
            
            // get the column expression, should be left, but also check the right operand if the left one is not column
            auto column_expression = std::dynamic_pointer_cast<LQPColumnExpression>(predicate->left_operand());
            auto value_expression = std::dynamic_pointer_cast<ValueExpression>(predicate->right_operand());
            if (!column_expression) {
                column_expression = std::dynamic_pointer_cast<LQPColumnExpression>(predicate->right_operand());
                value_expression = std::dynamic_pointer_cast<ValueExpression>(predicate->left_operand());
            }

            if ((!column_expression) || (!value_expression)) {
                // predicate needs to look like column = value or value = column
                return LQPVisitation::VisitInputs;
            }

            if (column_expression == column_candidate) {
                // equal condition and join column -> candidate for UCC
                const auto table = std::static_pointer_cast<const StoredTableNode>(column_expression->original_node.lock());
                candidates->push_back(UCCCandidate{table->table_name, column_expression->original_column_id});
                return LQPVisitation::VisitInputs;
            } 
            
            const auto expression_table = std::static_pointer_cast<const StoredTableNode>(column_expression->original_node.lock());
            const auto candidate_table = std::static_pointer_cast<const StoredTableNode>(column_candidate->original_node.lock());

            if (expression_table == candidate_table) {
                // both columns of same table -> if both are UCC we could still convert join to predicate, so both are UCC candidate
                candidates->push_back(UCCCandidate{expression_table->table_name, column_expression->original_column_id});
                candidates->push_back(UCCCandidate{candidate_table->table_name, column_candidate->original_column_id});
            }

            return LQPVisitation::VisitInputs;
        });
    }

    return candidates;
}

UCCCandidates* JoinToLocalPredicateRewritePlugin::identify_ucc_candidates() {
    const auto snapshot = Hyrise::get().default_lqp_cache->snapshot();

    auto ucc_candidates = new UCCCandidates{};

    for (const auto& [query, entry] : snapshot) {
        // std::cout << "\n" << query << std::endl;
        const auto& root_node = entry.value;

        // TODO: really use a vector, or deduplicate by using a set?

        visit_lqp(root_node, [&](auto& node) {
            const auto type = node->type;
            if ((type != LQPNodeType::Join) && (type != LQPNodeType::Aggregate)) {
                // Non-Join and Non-Aggregate (Groupby) nodes are not considered for optimization using UCCs
                return LQPVisitation::VisitInputs;
            }

            // std::cout << "\t" << node->description() << std::endl;

            if (type == LQPNodeType::Aggregate) {
                // in case of aggregate, extract all predicates used in groupby operations, then try to generate UCCCandidate objects from each of them
                auto& aggregate_node = static_cast<AggregateNode&>(*node);
                auto column_candidates = std::vector<std::shared_ptr<AbstractExpression>>{aggregate_node.node_expressions.begin(), aggregate_node.node_expressions.begin() + aggregate_node.aggregate_expressions_begin_idx};
                
                for (const auto& column_candidate : column_candidates) {
                    const auto casted_candidate = std::dynamic_pointer_cast<LQPColumnExpression>(column_candidate);
                    if (!casted_candidate) {
                        continue;
                    }
                    // every ColumnExpression used as a GroupBy expression should be checked for uniqueness
                    const auto table = std::static_pointer_cast<const StoredTableNode>(casted_candidate->original_node.lock());
                    const auto candidate = UCCCandidate{table->table_name, casted_candidate->original_column_id};
                    // std::cout << "\t\t\tAdding candidate " << candidate->table_name() << candidate->column_id() << std::endl;
                    ucc_candidates->insert(candidate);
                }
                
                return LQPVisitation::VisitInputs;
            }
            
            // if not aggregate node, must be a join node
            auto& join_node = static_cast<JoinNode&>(*node);
            const auto& join_mode = join_node.join_mode;
            // get join predicate with equals condition, that's the only one we would want to work on
            std::shared_ptr<BinaryPredicateExpression> join_predicate = nullptr;
            for (auto predicate : join_node.join_predicates()) {
                join_predicate = std::dynamic_pointer_cast<BinaryPredicateExpression>(predicate);
                if (!join_predicate) {
                    continue;
                }
                if (join_predicate->predicate_condition != PredicateCondition::Equals) {
                    join_predicate = nullptr;
                    continue;
                }
                break;
            }

            if (!join_predicate) {
                return LQPVisitation::VisitInputs;
            }

            // we only care about semi, inner (both are potential candidates), right outer (left is potential candidate) and left outer (right is potential candidate) joins
            switch (join_mode) {
                case JoinMode::Right: {
                    // want to check only the left hand side here, as this is the one that will be removed in the end
                    auto subtree_root = join_node.left_input();
                    // predicate may be swapped, so get proper operand
                    auto column_candidate = std::static_pointer_cast<LQPColumnExpression>(join_predicate->left_operand());
                    if (!expression_evaluable_on_lqp(column_candidate, *subtree_root.get())) {
                        column_candidate = std::static_pointer_cast<LQPColumnExpression>(join_predicate->right_operand());
                    }

                    // std::cout << "\t\tChecking for candidate " << column_candidate->as_column_name() << std::endl;
                    auto candidates = generate_valid_candidates(subtree_root, column_candidate);
                    if (candidates) {
                        for (const auto& candidate : *candidates) {
                            // std::cout << "\t\t\tAdding candidate " << candidate.table_name() << candidate.column_id() << std::endl;
                            ucc_candidates->insert(candidate);
                        }
                    }
                    break;
                }

                case JoinMode::Inner: {
                    auto column_candidates = std::vector<std::shared_ptr<AbstractExpression>>{join_predicate->left_operand(), join_predicate->right_operand()};
                    for (const auto& column_candidate : column_candidates) {
                        // std::cout << "\t\tChecking for candidate " << column_candidate->as_column_name() << std::endl;
                        // determine which subtree (left or right) belongs to the ColumnExpression
                        auto subtree_root = join_node.left_input();
                        if (!expression_evaluable_on_lqp(column_candidate, *subtree_root.get())) {
                            subtree_root = join_node.right_input();
                        }
                        auto candidates = generate_valid_candidates(subtree_root, std::static_pointer_cast<LQPColumnExpression>(column_candidate));
                        if (candidates) {
                            for (const auto& candidate : *candidates) {
                                // std::cout << "\t\t\tAdding candidate " << candidate.table_name() << candidate.column_id() << std::endl;
                                ucc_candidates->insert(candidate);
                            }
                        }
                    }
                    break;
                }

                case JoinMode::Semi:
                case JoinMode::Left: {
                    // want to check only the right hand side here, as this is the one that will be removed in the end
                    auto subtree_root = join_node.right_input();
                    // predicate may be swapped, so get proper operand
                    auto column_candidate = std::static_pointer_cast<LQPColumnExpression>(join_predicate->right_operand());
                    if (!expression_evaluable_on_lqp(column_candidate, *subtree_root.get())) {
                        column_candidate = std::static_pointer_cast<LQPColumnExpression>(join_predicate->left_operand());
                    }

                    // std::cout << "\t\tChecking for candidate " << column_candidate->as_column_name() << std::endl;
                    
                    auto candidates = generate_valid_candidates(subtree_root, column_candidate);
                    if (candidates) {
                        for (const auto& candidate : *candidates) {
                            // std::cout << "\t\t\tAdding candidate " << candidate.table_name() << candidate.column_id() << std::endl;
                            ucc_candidates->insert(candidate);
                        }
                    }
                    break;
                }

                default: {}
            }

            // TO DO

            // Identify whether the join operation could be improved by using UCCs
            // .. Check Node type - we only want to utilize UCCs for join elimination and Aggregation simplifying
            // -- which join types can actually be optimized? Only Inner? In case of left/right inner/outer, the one being dropped is the interesting side
            // ... If we see join, check whether predicate filter is done on same table and different column prior to join
            // ... Column being filtered on and join column are candidates
            // INTERFACE: UCC Validation Candidate: std::vector of table name & column ID -> get table name by traversing to leaf, then it's instance variable of StoredTableNode
            // Validate UCCs
            // .. Iterate over UCCCandidates; For each candidate:
            // ... Get all of its segments
            // ... Start with Dictionary Segments -> if len(dictionary) == len(attributevector) then UCC constraint still intact
            // ... If all individual segments match constraint, do value validation -> merge dictionaries, if collision UCC broken
            // ... If still everything valid, look at non-dictionary segments, check whether all values stored are not yet in your merged value list
            // --- If we want to use the GroupBy (Aggregation operator) -> put all values in one big vector, run operator on it, check whether #bins == #values
            // ... Add information gained to table -> also remember which columns are not UCCs?

            return LQPVisitation::VisitInputs;
        });
    }

    // std::cout << "------------------" << std::endl;
    // std::cout << "UCC candidates: " << ucc_candidates->size() << std::endl;

    return ucc_candidates;
}

EXPORT_PLUGIN(JoinToLocalPredicateRewritePlugin)

}

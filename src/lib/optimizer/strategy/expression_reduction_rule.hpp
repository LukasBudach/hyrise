#pragma once

#include <unordered_map>

#include "abstract_rule.hpp"
#include "expression/abstract_expression.hpp"
#include "expression/logical_expression.hpp"

namespace opossum {

class AbstractLQPNode;

/**
 * This rule tries to simplify logical expressions, so that they can be processed faster during operator execution or
 *   be further handled by other Optimizer rules.
 *
 * Reverse Distributivity
 *   Expressions of the form `(a AND b) OR (a AND c)` are rewritten as `a AND (b OR c)`. Conjunctive chains, i.e.,
 *   `(a OR b) AND (a OR c) -> a OR (b AND c)` are currently ignored as it is has not been determined yet whether
 *   one or the other form executes faster (TODO(anybody))
 *
 * Constant folding
 *   Expression involving only constants (`5 + 3 * 4`) are calculated and replaced with their result.
 */
class ExpressionReductionRule : public AbstractRule {
 public:
  std::string name() const override;

  /**
   * Use the law of boolean distributivity to reduce an expression
   * `(a AND b) OR (a AND c)` becomes `a AND (b OR c)`
   */
  static const std::shared_ptr<AbstractExpression>& reduce_distributivity(
      std::shared_ptr<AbstractExpression>& input_expression);

  /**
   * Rewrite `5 + 3` to `8`
   */
  static void reduce_constant_expression(std::shared_ptr<AbstractExpression>& input_expression);

  /**
   * Rewrite `SELECT SUM(a), COUNT(a), AVG(a)` to `SELECT SUM(a), COUNT(a), SUM(a) / COUNT(a) AS AVG(a)`
   */
  static void remove_duplicate_aggregate(std::vector<std::shared_ptr<AbstractExpression>>& input_expressions,
                                         const std::shared_ptr<AbstractLQPNode>& aggregate_node,
                                         const std::shared_ptr<AbstractLQPNode>& root_node);

 protected:
  void _apply_to_plan_without_subqueries(const std::shared_ptr<AbstractLQPNode>& lqp_root) const override;
};

}  // namespace opossum

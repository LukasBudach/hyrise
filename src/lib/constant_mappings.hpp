#include <boost/bimap.hpp>
#include <string>
#include <unordered_map>

#include "sql/Expr.h"
#include "sql/SelectStatement.h"

#include "all_type_variant.hpp"
#include "expression/function_expression.hpp"
#include "expression/logical_expression.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "types.hpp"

namespace opossum {

enum class EncodingType : uint8_t;
enum class VectorCompressionType : uint8_t;
enum class AggregateFunction;
enum class ExpressionType;
enum class LogicalOperator;
enum class TableType;
enum class OperatorType;
enum class ScanType : uint8_t;

extern const boost::bimap<PredicateCondition, std::string> predicate_condition_to_string;
extern const std::unordered_map<hsql::OrderType, OrderByMode> order_type_to_order_by_mode;
extern const std::unordered_map<ExpressionType, std::string> expression_type_to_string;
extern const std::unordered_map<JoinType, std::string> join_type_to_string;
extern const std::unordered_map<LQPNodeType, std::string> lqp_node_type_to_string;
extern const std::unordered_map<OperatorType, std::string> operator_type_to_string;
extern const std::unordered_map<ScanType, std::string> scan_type_to_string;
extern const boost::bimap<AggregateFunction, std::string> aggregate_function_to_string;
extern const boost::bimap<FunctionType, std::string> function_type_to_string;
extern const boost::bimap<DataType, std::string> data_type_to_string;
extern const boost::bimap<EncodingType, std::string> encoding_type_to_string;
extern const boost::bimap<LogicalOperator, std::string> logical_operator_to_string;
extern const boost::bimap<VectorCompressionType, std::string> vector_compression_type_to_string;

std::ostream& operator<<(std::ostream& stream, AggregateFunction aggregate_function);
std::ostream& operator<<(std::ostream& stream, FunctionType function_type);
std::ostream& operator<<(std::ostream& stream, DataType data_type);
std::ostream& operator<<(std::ostream& stream, EncodingType encoding_type);
std::ostream& operator<<(std::ostream& stream, VectorCompressionType vector_compression_type);

}  // namespace opossum

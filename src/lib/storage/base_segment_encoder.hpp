#pragma once

#include <memory>
#include <type_traits>

#include <boost/hana/type.hpp>

#include "all_type_variant.hpp"
#include "resolve_type.hpp"
#include "storage/abstract_encoded_segment.hpp"
#include "storage/abstract_segment.hpp"
#include "storage/create_iterable_from_segment.hpp"
#include "storage/encoding_type.hpp"
#include "storage/vector_compression/vector_compression.hpp"
#include "types.hpp"
#include "utils/assert.hpp"

namespace opossum {

namespace hana = boost::hana;

class LZ4Encoder;

/**
 * @brief Base class of all segment encoders
 */
class BaseSegmentEncoder {
 public:
  virtual ~BaseSegmentEncoder() = default;

  /**
   * @brief Returns true if the encoder supports the given data type.
   */
  virtual bool supports(DataType data_type) const = 0;

  /**
   * @brief Encodes a value segment that has the given data type.
   *
   * @return encoded segment if data type is supported else throws exception
   */
  virtual std::shared_ptr<AbstractEncodedSegment> encode(const std::shared_ptr<const AbstractSegment>& segment,
                                                         DataType data_type) = 0;

  virtual std::unique_ptr<BaseSegmentEncoder> create_new() const = 0;

  /**
   * @defgroup Interface for selecting the used vector compression type
   *
   * Many encoding schemes use the following principle to compress data:
   * Replace a set of large integers (or values of any data type) with
   * a set of mostly smaller integers using an invertible transformation.
   * Compress the resulting set using vector compression (null suppression).
   *
   * @{
   */

  virtual bool uses_vector_compression() const = 0;
  virtual void set_vector_compression(VectorCompressionType type) = 0;
  /**@}*/
};

template <typename Derived>
class SegmentEncoder : public BaseSegmentEncoder {
 public:
  /**
   * @defgroup Virtual interface implementation
   * @{
   */
  bool supports(DataType data_type) const final {
    bool result{};
    resolve_data_type(data_type, [&](auto data_type_c) { result = this->supports(data_type_c); });
    return result;
  }

  // Resolves the data type and calls the appropriate instantiation of encode().
  std::shared_ptr<AbstractEncodedSegment> encode(const std::shared_ptr<const AbstractSegment>& segment,
                                                 DataType data_type) final {
    auto encoded_segment = std::shared_ptr<AbstractEncodedSegment>{};
    resolve_data_type(data_type, [&](auto data_type_c) {
      const auto data_type_supported = this->supports(data_type_c);
      // clang-format off
      if constexpr (hana::value(data_type_supported)) {
        /**
         * The templated method encode() where the actual encoding happens
         * is only instantiated for data types supported by the encoding type.
         */
        encoded_segment = this->encode(segment, data_type_c);
      } else {
        Fail("Passed data type not supported by encoding.");
      }
      // clang-format on
    });

    return encoded_segment;
  }

  std::unique_ptr<BaseSegmentEncoder> create_new() const final {
    return std::make_unique<Derived>();
  }

  bool uses_vector_compression() const final {
    return Derived::_uses_vector_compression;
  }

  void set_vector_compression(VectorCompressionType type) final {
    Assert(uses_vector_compression(), "Vector compression type can only be set if supported by encoder.");

    _vector_compression_type = type;
  }
  /**@}*/

 public:
  /**
   * @defgroup Non-virtual interface
   * @{
   */

  /**
   * @return an integral constant implicitly convertible to bool
   *
   * Since this method is used in compile-time expression,
   * it cannot simply return bool.
   *
   * Hint: Use hana::value() if you want to use the result
   *       in a constant expression such as constexpr-if.
   */
  template <typename ColumnDataType>
  auto supports(hana::basic_type<ColumnDataType> data_type) const {
    return encoding_supports_data_type(Derived::_encoding_type, data_type);
  }

  /**
   * @brief Encodes a value segment with the given data type.
   *
   * Compiles only for supported data types.
   */
  template <typename ColumnDataType>
  std::shared_ptr<AbstractEncodedSegment> encode(const std::shared_ptr<const AbstractSegment>& abstract_segment,
                                                 hana::basic_type<ColumnDataType> data_type_c) {
    static_assert(decltype(supports(data_type_c))::value);
    const auto iterable = create_any_segment_iterable<ColumnDataType>(*abstract_segment);

    // For now, we allocate without a specific memory source.
    return _self()._on_encode(iterable, PolymorphicAllocator<ColumnDataType>{});
  }
  /**@}*/

 protected:
  VectorCompressionType vector_compression_type() const {
    return _vector_compression_type;
  }

 private:
  // LZ4Encoder only supports BitPacking in order to reduce the compile time, see the comment in lz4_encoder.hpp.
  VectorCompressionType _vector_compression_type = std::is_same_v<Derived, LZ4Encoder>
                                                       ? VectorCompressionType::BitPacking
                                                       : VectorCompressionType::FixedWidthInteger;

 private:
  Derived& _self() {
    return static_cast<Derived&>(*this);
  }

  const Derived& _self() const {
    return static_cast<const Derived&>(*this);
  }
};

}  // namespace opossum

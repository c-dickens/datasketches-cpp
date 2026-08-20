/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef MINHASH_SKETCH_HPP_
#define MINHASH_SKETCH_HPP_

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "common_defs.hpp"

namespace datasketches {

/// MinHash sketch constants
namespace minhash_constants {
  /// default number of hash slots
  const uint16_t DEFAULT_K = 256;
}

// forward declaration
template<typename A> class minhash_sketch_alloc;

/// MinHash sketch alias with default allocator
using minhash_sketch = minhash_sketch_alloc<std::allocator<uint64_t>>;

namespace minhash_helpers {

// double value canonicalization for compatibility with Java
// (local copy of the theta sketch helper to keep this component self-contained)
static inline int64_t canonical_double(double value) {
  union {
    int64_t long_value;
    double double_value;
  } long_double_union;

  if (value == 0.0) {
    long_double_union.double_value = 0.0; // canonicalize -0.0 to 0.0
  } else if (std::isnan(value)) {
    long_double_union.long_value = 0x7ff8000000000000L; // canonicalize NaN using value from Java's Double.doubleToLongBits()
  } else {
    long_double_union.double_value = value;
  }
  return long_double_union.long_value;
}

} /* namespace minhash_helpers */

/**
 * MinHash sketch: a fixed-length signature of a set that supports estimating
 * the Jaccard similarity between sets and locality-sensitive hashing (LSH) banding.
 *
 * The sketch maintains k slots. Each update hashes the item once with MurmurHash3
 * and derives one value per slot as g_i = h1 + i * (h2 | 1) with wrapping arithmetic
 * (Kirsch-Mitzenmacher double hashing); each slot retains the minimum value seen.
 * Two sketches built with the same k and seed are positionally aligned, and the
 * fraction of equal slots is an estimate of the Jaccard similarity of the
 * underlying sets.
 *
 * Unlike most sketches in this library, k may be any value >= 1, not only a power
 * of 2. This keeps the lattice of LSH banding configurations (b bands of r rows
 * requires b * r = k) and their threshold midpoints (1/b)^(1/r) dense.
 *
 * The theta sketch also estimates pairwise Jaccard similarity; the reason this
 * sketch exists is that its positionally aligned signature supports LSH banding
 * for sub-quadratic all-pairs similarity search, which theta cannot do.
 */
template<typename Allocator = std::allocator<uint64_t>>
class minhash_sketch_alloc {
public:
  using allocator_type = Allocator;
  using vector_u64 = std::vector<uint64_t, Allocator>;

  class builder;

  /// @return true if this sketch has never been updated
  bool is_empty() const;

  /// @return the number of hash slots k
  uint32_t get_num_hashes() const;

  /// @return the hash seed
  uint64_t get_seed() const;

  /**
   * Signature accessor, primarily for LSH banding and testing.
   * @return the k-slot signature (every slot is UINT64_MAX until the first update)
   */
  const vector_u64& get_signature() const;

  /// @return allocator
  allocator_type get_allocator() const;

  /**
   * Update this sketch with a given unsigned 64-bit integer.
   * @param value uint64_t to update the sketch with
   */
  void update(uint64_t value);

  /**
   * Update this sketch with a given signed 64-bit integer.
   * @param value int64_t to update the sketch with
   */
  void update(int64_t value);

  /**
   * Update this sketch with a given unsigned 32-bit integer.
   * For compatibility with Java implementation.
   * @param value uint32_t to update the sketch with
   */
  void update(uint32_t value);

  /**
   * Update this sketch with a given signed 32-bit integer.
   * For compatibility with Java implementation.
   * @param value int32_t to update the sketch with
   */
  void update(int32_t value);

  /**
   * Update this sketch with a given unsigned 16-bit integer.
   * For compatibility with Java implementation.
   * @param value uint16_t to update the sketch with
   */
  void update(uint16_t value);

  /**
   * Update this sketch with a given signed 16-bit integer.
   * For compatibility with Java implementation.
   * @param value int16_t to update the sketch with
   */
  void update(int16_t value);

  /**
   * Update this sketch with a given unsigned 8-bit integer.
   * For compatibility with Java implementation.
   * @param value uint8_t to update the sketch with
   */
  void update(uint8_t value);

  /**
   * Update this sketch with a given signed 8-bit integer.
   * For compatibility with Java implementation.
   * @param value int8_t to update the sketch with
   */
  void update(int8_t value);

  /**
   * Update this sketch with a given double-precision floating point value.
   * For compatibility with Java implementation.
   * Note: this canonicalizes the bit pattern of the double, so update(1.0)
   * is a different item than update(1).
   * @param value double to update the sketch with
   */
  void update(double value);

  /**
   * Update this sketch with a given floating point value.
   * For compatibility with Java implementation.
   * @param value float to update the sketch with
   */
  void update(float value);

  /**
   * Update this sketch with a given string.
   * An empty string is ignored.
   * @param value string to update the sketch with
   */
  void update(const std::string& value);

  /**
   * Update this sketch with given data of any type.
   * This is a "universal" update that covers all cases above,
   * but may produce different hashes.
   * Be very careful to hash input values consistently using the same approach
   * both over time and on different platforms.
   * @param data pointer to the data
   * @param length of the data in bytes
   */
  void update(const void* data, size_t length);

  /**
   * Merge the given sketch into this one (elementwise minimum of signatures).
   * The result is exactly the sketch that would have been built from the
   * union of the two input streams.
   * @param other sketch to merge into this one
   * @throws std::invalid_argument if k or seed do not match
   */
  void merge(const minhash_sketch_alloc& other);

  /**
   * Estimate of the Jaccard similarity between the set represented by this
   * sketch and the set represented by the given sketch (the fraction of
   * equal signature slots).
   * If both sketches are empty the estimate is 1 (equal empty sets);
   * if exactly one is empty the estimate is 0.
   * @param other sketch to compare this one with
   * @return estimated Jaccard similarity in [0, 1]
   * @throws std::invalid_argument if k or seed do not match
   */
  double jaccard(const minhash_sketch_alloc& other) const;

  /**
   * {lower bound, estimate, upper bound} of the Jaccard similarity between the
   * sets represented by this sketch and the given sketch, using approximate
   * Clopper-Pearson confidence bounds on the slot-match proportion.
   * Note: the k slot-match indicators are not fully independent under
   * Kirsch-Mitzenmacher slot derivation, so the coverage of these bounds is
   * approximate rather than exact.
   * If both sketches are empty the result is {1, 1, 1};
   * if exactly one is empty the result is {0, 0, 0}.
   * @param other sketch to compare this one with
   * @param num_std_devs number of standard deviations defining the confidence interval
   * @return array of {lower bound, estimate, upper bound}
   * @throws std::invalid_argument if k or seed do not match
   */
  std::array<double, 3> jaccard_bounds(const minhash_sketch_alloc& other, uint8_t num_std_devs = 2) const;

  /**
   * Prints a summary of the sketch.
   * @return the summary as a string
   */
  string<Allocator> to_string() const;

  using vector_bytes = std::vector<uint8_t, typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t>>;

  /**
   * Computes size in bytes required to serialize the current state of the sketch.
   * @return size in bytes
   */
  size_t get_serialized_size_bytes() const;

  /**
   * This method serializes the sketch into a given stream in a binary form.
   * @param os output stream
   */
  void serialize(std::ostream& os) const;

  /**
   * This method serializes the sketch as a vector of bytes.
   * An optional header can be reserved in front of the sketch.
   * It is a blank space of a given size.
   * @param header_size_bytes space to reserve in front of the sketch
   * @return serialized sketch as a vector of bytes
   */
  vector_bytes serialize(unsigned header_size_bytes = 0) const;

  /**
   * This method deserializes a sketch from a given stream.
   * @param is input stream
   * @param seed the seed the serialized sketch was built with
   * @param allocator instance of an Allocator
   * @return an instance of the sketch
   * @throws std::invalid_argument on malformed or incompatible preamble or seed mismatch
   * @throws std::runtime_error if reading from the stream fails
   */
  static minhash_sketch_alloc deserialize(std::istream& is, uint64_t seed = DEFAULT_SEED,
      const Allocator& allocator = Allocator());

  /**
   * This method deserializes a sketch from a given array of bytes.
   * @param bytes pointer to the array of bytes
   * @param size the size of the array
   * @param seed the seed the serialized sketch was built with
   * @param allocator instance of an Allocator
   * @return an instance of the sketch
   * @throws std::invalid_argument on malformed or incompatible preamble or seed mismatch
   * @throws std::out_of_range if the given array is too small
   */
  static minhash_sketch_alloc deserialize(const void* bytes, size_t size, uint64_t seed = DEFAULT_SEED,
      const Allocator& allocator = Allocator());

private:
  /*
   * Serialized sketch layout (16-byte preamble, then the signature):
   * Byte 0: preamble longs (2)
   * Byte 1: serial version (1)
   * Byte 2: family id (23 - drafted on the dev@datasketches thread, NOT officially
   *         assigned yet; must be confirmed cross-language before this format ships)
   * Byte 3: flags (bit 0: is_empty)
   * Bytes 4-5: k (uint16)
   * Bytes 6-7: seed hash
   * Bytes 8-15: unused (zeros)
   * Bytes 16+: k signature words of 8 bytes each (omitted when the sketch is empty)
   */
  static const uint8_t PREAMBLE_LONGS = 2;
  static const uint8_t SERIAL_VERSION = 1;
  static const uint8_t FAMILY_ID = 23;
  enum flags { IS_EMPTY };

  bool is_empty_;
  uint64_t seed_;
  vector_u64 signature_;

  minhash_sketch_alloc(uint16_t k, uint64_t seed, const Allocator& allocator);

  void check_compatible(const minhash_sketch_alloc& other) const;
  uint32_t count_matches(const minhash_sketch_alloc& other) const;
  static void check_header_validity(uint8_t preamble_longs, uint8_t serial_version,
      uint8_t family_id, uint16_t k);
};

/// MinHash sketch builder
template<typename Allocator>
class minhash_sketch_alloc<Allocator>::builder {
public:
  /**
   * Creates and instance of the builder with default parameters.
   * @param allocator instance of an Allocator to pass to created sketches
   */
  explicit builder(const Allocator& allocator = Allocator());

  /**
   * Sets the number of hash slots k. Any value >= 1 is valid (k does not
   * have to be a power of 2). The default is 256.
   * @param k number of hash slots
   * @return this builder
   * @throws std::invalid_argument if k is zero
   */
  builder& set_k(uint16_t k);

  /**
   * Sets the seed for the hash function.
   * Sketches must have the same seed to be compared or merged.
   * @param seed hash seed
   * @return this builder
   */
  builder& set_seed(uint64_t seed);

  /**
   * Creates a sketch with the current configuration of the builder.
   * @return a MinHash sketch
   */
  minhash_sketch_alloc build() const;

private:
  uint16_t k_;
  uint64_t seed_;
  Allocator allocator_;
};

} /* namespace datasketches */

#include "minhash_sketch_impl.hpp"

#endif

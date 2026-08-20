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

#ifndef MINHASH_SKETCH_IMPL_HPP_
#define MINHASH_SKETCH_IMPL_HPP_

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "MurmurHash3.h"
#include "bounds_binomial_proportions.hpp"
#include "memory_operations.hpp"

namespace datasketches {

template<typename A>
minhash_sketch_alloc<A>::minhash_sketch_alloc(uint16_t k, uint64_t seed, const A& allocator):
is_empty_(true),
seed_(seed),
signature_(k, std::numeric_limits<uint64_t>::max(), allocator)
{}

template<typename A>
bool minhash_sketch_alloc<A>::is_empty() const {
  return is_empty_;
}

template<typename A>
uint32_t minhash_sketch_alloc<A>::get_num_hashes() const {
  return static_cast<uint32_t>(signature_.size());
}

template<typename A>
uint64_t minhash_sketch_alloc<A>::get_seed() const {
  return seed_;
}

template<typename A>
auto minhash_sketch_alloc<A>::get_signature() const -> const vector_u64& {
  return signature_;
}

template<typename A>
auto minhash_sketch_alloc<A>::get_allocator() const -> allocator_type {
  return signature_.get_allocator();
}

template<typename A>
void minhash_sketch_alloc<A>::update(uint64_t value) {
  update(&value, sizeof(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(int64_t value) {
  update(&value, sizeof(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(uint32_t value) {
  update(static_cast<int32_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(int32_t value) {
  update(static_cast<int64_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(uint16_t value) {
  update(static_cast<int16_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(int16_t value) {
  update(static_cast<int64_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(uint8_t value) {
  update(static_cast<int8_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(int8_t value) {
  update(static_cast<int64_t>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(double value) {
  update(minhash_helpers::canonical_double(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(float value) {
  update(static_cast<double>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(const std::string& value) {
  if (value.empty()) return;
  update(value.c_str(), value.length());
}

template<typename A>
void minhash_sketch_alloc<A>::update(const void* data, size_t length) {
  HashState hashes;
  MurmurHash3_x64_128(data, length, seed_, hashes);
  // Kirsch-Mitzenmacher double hashing: slot value g_i = h1 + i * (h2 | 1).
  // The odd increment makes the k slot values distinct; unsigned wrapping is intended.
  const uint64_t increment = hashes.h2 | 1;
  uint64_t value = hashes.h1;
  for (uint64_t& slot: signature_) {
    slot = std::min(slot, value);
    value += increment;
  }
  is_empty_ = false;
}

template<typename A>
void minhash_sketch_alloc<A>::merge(const minhash_sketch_alloc& other) {
  check_compatible(other);
  std::transform(signature_.begin(), signature_.end(), other.signature_.begin(), signature_.begin(),
      [](uint64_t a, uint64_t b) { return std::min(a, b); });
  is_empty_ = is_empty_ && other.is_empty_;
}

template<typename A>
double minhash_sketch_alloc<A>::jaccard(const minhash_sketch_alloc& other) const {
  check_compatible(other);
  if (is_empty_ && other.is_empty_) return 1.0; // both represent the empty set
  if (is_empty_ || other.is_empty_) return 0.0;
  return static_cast<double>(count_matches(other)) / static_cast<double>(signature_.size());
}

template<typename A>
std::array<double, 3> minhash_sketch_alloc<A>::jaccard_bounds(const minhash_sketch_alloc& other,
    uint8_t num_std_devs) const {
  check_compatible(other);
  if (is_empty_ && other.is_empty_) return {1.0, 1.0, 1.0}; // both represent the empty set
  if (is_empty_ || other.is_empty_) return {0.0, 0.0, 0.0};
  const uint64_t n = signature_.size();
  const uint64_t matches = count_matches(other);
  return {
    bounds_binomial_proportions::approximate_lower_bound_on_p(n, matches, num_std_devs),
    static_cast<double>(matches) / static_cast<double>(n),
    bounds_binomial_proportions::approximate_upper_bound_on_p(n, matches, num_std_devs)
  };
}

template<typename A>
string<A> minhash_sketch_alloc<A>::to_string() const {
  // Using a temporary stream for implementation here does not comply with AllocatorAwareContainer requirements.
  // The stream does not support passing an allocator instance, and alternatives are complicated.
  std::ostringstream os;
  os << "### MinHash sketch summary:" << std::endl;
  os << "   k (num hashes) : " << get_num_hashes() << std::endl;
  os << "   seed hash      : " << compute_seed_hash(seed_) << std::endl;
  os << "   empty?         : " << (is_empty() ? "true" : "false") << std::endl;
  os << "### End sketch summary" << std::endl;
  return string<A>(os.str().c_str(), get_allocator());
}

template<typename A>
void minhash_sketch_alloc<A>::check_compatible(const minhash_sketch_alloc& other) const {
  if (signature_.size() != other.signature_.size()) {
    throw std::invalid_argument("incompatible sketches: k " + std::to_string(signature_.size())
        + " vs " + std::to_string(other.signature_.size()));
  }
  if (seed_ != other.seed_) {
    throw std::invalid_argument("incompatible sketches: seeds differ");
  }
}

template<typename A>
uint32_t minhash_sketch_alloc<A>::count_matches(const minhash_sketch_alloc& other) const {
  uint32_t matches = 0;
  for (size_t i = 0; i < signature_.size(); ++i) {
    if (signature_[i] == other.signature_[i]) ++matches;
  }
  return matches;
}

template<typename A>
size_t minhash_sketch_alloc<A>::get_serialized_size_bytes() const {
  return PREAMBLE_LONGS * sizeof(uint64_t) + (is_empty_ ? 0 : signature_.size() * sizeof(uint64_t));
}

template<typename A>
void minhash_sketch_alloc<A>::serialize(std::ostream& os) const {
  const uint8_t preamble_longs = PREAMBLE_LONGS;
  write(os, preamble_longs);
  const uint8_t serial_version = SERIAL_VERSION;
  write(os, serial_version);
  const uint8_t family_id = FAMILY_ID;
  write(os, family_id);
  const uint8_t flags_byte = (is_empty_ ? 1 << flags::IS_EMPTY : 0);
  write(os, flags_byte);
  const uint16_t k = static_cast<uint16_t>(signature_.size());
  write(os, k);
  const uint16_t seed_hash = compute_seed_hash(seed_);
  write(os, seed_hash);
  const uint64_t unused64 = 0;
  write(os, unused64);
  if (is_empty_) return;
  write(os, signature_.data(), signature_.size() * sizeof(uint64_t));
}

template<typename A>
auto minhash_sketch_alloc<A>::serialize(unsigned header_size_bytes) const -> vector_bytes {
  vector_bytes bytes(header_size_bytes + get_serialized_size_bytes(), 0, signature_.get_allocator());
  uint8_t* ptr = bytes.data() + header_size_bytes;
  const uint8_t preamble_longs = PREAMBLE_LONGS;
  ptr += copy_to_mem(preamble_longs, ptr);
  const uint8_t serial_version = SERIAL_VERSION;
  ptr += copy_to_mem(serial_version, ptr);
  const uint8_t family_id = FAMILY_ID;
  ptr += copy_to_mem(family_id, ptr);
  const uint8_t flags_byte = (is_empty_ ? 1 << flags::IS_EMPTY : 0);
  ptr += copy_to_mem(flags_byte, ptr);
  const uint16_t k = static_cast<uint16_t>(signature_.size());
  ptr += copy_to_mem(k, ptr);
  const uint16_t seed_hash = compute_seed_hash(seed_);
  ptr += copy_to_mem(seed_hash, ptr);
  const uint64_t unused64 = 0;
  ptr += copy_to_mem(unused64, ptr);
  if (!is_empty_) {
    copy_to_mem(signature_.data(), ptr, signature_.size() * sizeof(uint64_t));
  }
  return bytes;
}

template<typename A>
auto minhash_sketch_alloc<A>::deserialize(std::istream& is, uint64_t seed, const A& allocator)
    -> minhash_sketch_alloc {
  const auto preamble_longs = read<uint8_t>(is);
  const auto serial_version = read<uint8_t>(is);
  const auto family_id = read<uint8_t>(is);
  const auto flags_byte = read<uint8_t>(is);
  const auto k = read<uint16_t>(is);
  const auto seed_hash = read<uint16_t>(is);
  read<uint64_t>(is); // unused
  if (!is.good()) throw std::runtime_error("error reading from std::istream");
  check_header_validity(preamble_longs, serial_version, family_id, k);
  if (seed_hash != compute_seed_hash(seed)) {
    throw std::invalid_argument("incompatible seed hashes: " + std::to_string(seed_hash) + ", "
        + std::to_string(compute_seed_hash(seed)));
  }
  minhash_sketch_alloc sketch(k, seed, allocator);
  if ((flags_byte & (1 << flags::IS_EMPTY)) == 0) {
    read(is, sketch.signature_.data(), static_cast<size_t>(k) * sizeof(uint64_t));
    if (!is.good()) throw std::runtime_error("error reading from std::istream");
    sketch.is_empty_ = false;
  }
  return sketch;
}

template<typename A>
auto minhash_sketch_alloc<A>::deserialize(const void* bytes, size_t size, uint64_t seed,
    const A& allocator) -> minhash_sketch_alloc {
  ensure_minimum_memory(size, PREAMBLE_LONGS * sizeof(uint64_t));
  const char* ptr = static_cast<const char*>(bytes);
  uint8_t preamble_longs;
  ptr += copy_from_mem(ptr, preamble_longs);
  uint8_t serial_version;
  ptr += copy_from_mem(ptr, serial_version);
  uint8_t family_id;
  ptr += copy_from_mem(ptr, family_id);
  uint8_t flags_byte;
  ptr += copy_from_mem(ptr, flags_byte);
  uint16_t k;
  ptr += copy_from_mem(ptr, k);
  uint16_t seed_hash;
  ptr += copy_from_mem(ptr, seed_hash);
  ptr += sizeof(uint64_t); // unused
  check_header_validity(preamble_longs, serial_version, family_id, k);
  if (seed_hash != compute_seed_hash(seed)) {
    throw std::invalid_argument("incompatible seed hashes: " + std::to_string(seed_hash) + ", "
        + std::to_string(compute_seed_hash(seed)));
  }
  minhash_sketch_alloc sketch(k, seed, allocator);
  if ((flags_byte & (1 << flags::IS_EMPTY)) == 0) {
    ensure_minimum_memory(size, PREAMBLE_LONGS * sizeof(uint64_t)
        + static_cast<size_t>(k) * sizeof(uint64_t));
    copy_from_mem(ptr, sketch.signature_.data(), static_cast<size_t>(k) * sizeof(uint64_t));
    sketch.is_empty_ = false;
  }
  return sketch;
}

template<typename A>
void minhash_sketch_alloc<A>::check_header_validity(uint8_t preamble_longs, uint8_t serial_version,
    uint8_t family_id, uint16_t k) {
  if (preamble_longs != PREAMBLE_LONGS || serial_version != SERIAL_VERSION
      || family_id != FAMILY_ID || k == 0) {
    throw std::invalid_argument("possible corruption: preamble longs " + std::to_string(preamble_longs)
        + " (expected " + std::to_string(PREAMBLE_LONGS) + "), serial version "
        + std::to_string(serial_version) + " (expected " + std::to_string(SERIAL_VERSION)
        + "), family " + std::to_string(family_id) + " (expected " + std::to_string(FAMILY_ID)
        + "), k " + std::to_string(k) + " (expected at least 1)");
  }
}

// builder

template<typename A>
minhash_sketch_alloc<A>::builder::builder(const A& allocator):
k_(minhash_constants::DEFAULT_K),
seed_(DEFAULT_SEED),
allocator_(allocator)
{}

template<typename A>
auto minhash_sketch_alloc<A>::builder::set_k(uint16_t k) -> builder& {
  if (k == 0) throw std::invalid_argument("k must be at least 1");
  k_ = k;
  return *this;
}

template<typename A>
auto minhash_sketch_alloc<A>::builder::set_seed(uint64_t seed) -> builder& {
  seed_ = seed;
  return *this;
}

template<typename A>
auto minhash_sketch_alloc<A>::builder::build() const -> minhash_sketch_alloc {
  return minhash_sketch_alloc(k_, seed_, allocator_);
}

} /* namespace datasketches */

#endif

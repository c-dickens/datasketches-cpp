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
#include <sstream>
#include <stdexcept>

#include "MurmurHash3.h" // only for compute_seed_hash(), which every sketch shares; slots use XXH64
#include "bounds_binomial_proportions.hpp"
#include "canonical_double.hpp"
#include "xxhash64.h"

namespace datasketches {

template<typename A>
minhash_sketch_alloc<A>::minhash_sketch_alloc(uint16_t k, uint64_t seed, const A& allocator):
seed_(seed),
signature_(k, minhash_constants::EMPTY_MARKER, allocator)
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
uint16_t minhash_sketch_alloc<A>::get_seed_hash() const {
  return compute_seed_hash(seed_);
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
  // As in the bloom filter: XXHash64 reads these 8 bytes back as one host-order
  // word, so the hash depends only on the value and is the same on every platform.
  update(&value, sizeof(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(int64_t value) {
  update(&value, sizeof(value)); // see update(uint64_t)
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
  update(canonical_double(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(float value) {
  update(static_cast<double>(value));
}

template<typename A>
void minhash_sketch_alloc<A>::update(const std::string& value) {
  if (value.empty()) { return; }
  update(value.c_str(), value.length());
}

template<typename A>
void minhash_sketch_alloc<A>::update(const void* data, size_t length) {
  // One XXH64 hash per item, then one SplitMix64 output per slot (ADR-0002, SPEC.md).
  // All arithmetic is unsigned 64-bit and wraps; every shift is logical.
  uint64_t state = XXHash64::hash(data, length, seed_);
  for (uint64_t& slot: signature_) {
    state += minhash_constants::SPLITMIX64_GAMMA;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * minhash_constants::SPLITMIX64_MIX1;
    z = (z ^ (z >> 27)) * minhash_constants::SPLITMIX64_MIX2;
    z ^= z >> 31;
    slot = std::min(slot, z >> 1); // 63-bit slot values (ADR-0001)
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
  if (is_empty_ && other.is_empty_) { return 1.0; } // both represent the empty set
  if (is_empty_ || other.is_empty_) { return 0.0; }
  return static_cast<double>(count_matches(other)) / static_cast<double>(signature_.size());
}

template<typename A>
std::array<double, 3> minhash_sketch_alloc<A>::jaccard_bounds(const minhash_sketch_alloc& other,
    uint8_t num_std_devs) const {
  // same range as the theta bounds (binomial_bounds::check_num_std_devs)
  if (num_std_devs < 1 || num_std_devs > 3) {
    throw std::invalid_argument("num_std_devs must be 1, 2 or 3");
  }
  check_compatible(other);
  if (is_empty_ && other.is_empty_) { return {1.0, 1.0, 1.0}; } // both represent the empty set
  if (is_empty_ || other.is_empty_) { return {0.0, 0.0, 0.0}; }
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
    if (signature_[i] == other.signature_[i]) { ++matches; }
  }
  return matches;
}

// serialization (minhash/SPEC.md, "Serialized format, version 1")

template<typename A> const size_t minhash_sketch_alloc<A>::PREAMBLE_BYTES;
template<typename A> const uint8_t minhash_sketch_alloc<A>::FLAG_EMPTY;
template<typename A> const uint8_t minhash_sketch_alloc<A>::FLAG_HASH_LIST;
template<typename A> const uint8_t minhash_sketch_alloc<A>::FLAGS_UNASSIGNED;

template<typename A>
size_t minhash_sketch_alloc<A>::get_serialized_size_bytes() const {
  if (is_empty_) { return PREAMBLE_BYTES; }
  return PREAMBLE_BYTES + sizeof(uint64_t) * signature_.size();
}

template<typename A>
void minhash_sketch_alloc<A>::serialize(std::ostream& os) const {
  uint8_t buffer[PREAMBLE_BYTES];
  write_preamble(buffer);
  os.write(reinterpret_cast<const char*>(buffer), PREAMBLE_BYTES);
  if (is_empty_) { return; }
  for (const uint64_t slot: signature_) {
    store_u64(buffer, slot);
    os.write(reinterpret_cast<const char*>(buffer), sizeof(uint64_t));
  }
}

template<typename A>
auto minhash_sketch_alloc<A>::serialize(unsigned header_size_bytes) const -> vector_bytes {
  const size_t size = header_size_bytes + get_serialized_size_bytes();
  vector_bytes bytes(size, 0, AllocBytes(signature_.get_allocator()));
  uint8_t* ptr = bytes.data() + header_size_bytes;
  write_preamble(ptr);
  if (!is_empty_) {
    ptr += PREAMBLE_BYTES;
    for (const uint64_t slot: signature_) {
      store_u64(ptr, slot);
      ptr += sizeof(uint64_t);
    }
  }
  return bytes;
}

template<typename A>
minhash_sketch_alloc<A> minhash_sketch_alloc<A>::deserialize(std::istream& is, uint64_t seed, const A& allocator) {
  uint8_t buffer[PREAMBLE_BYTES];
  is.read(reinterpret_cast<char*>(buffer), PREAMBLE_BYTES);
  if (!is.good()) { throw std::runtime_error("error reading from std::istream"); }
  const preamble pre = check_preamble(buffer, seed);
  minhash_sketch_alloc sketch(pre.k, seed, allocator);
  if (pre.is_empty) { return sketch; }
  for (uint64_t& slot: sketch.signature_) {
    is.read(reinterpret_cast<char*>(buffer), sizeof(uint64_t));
    if (!is.good()) { throw std::runtime_error("error reading from std::istream"); }
    slot = check_slot_value(load_u64(buffer));
  }
  sketch.is_empty_ = false;
  return sketch;
}

template<typename A>
minhash_sketch_alloc<A> minhash_sketch_alloc<A>::deserialize(const void* bytes, size_t size, uint64_t seed,
    const A& allocator) {
  if (size < PREAMBLE_BYTES) {
    throw std::invalid_argument("size: at least " + std::to_string(PREAMBLE_BYTES)
        + " bytes expected, actual " + std::to_string(size));
  }
  const auto* ptr = static_cast<const uint8_t*>(bytes);
  const preamble pre = check_preamble(ptr, seed);
  if (!pre.is_empty) {
    const size_t expected = PREAMBLE_BYTES + sizeof(uint64_t) * pre.k;
    if (size < expected) {
      throw std::invalid_argument("size: at least " + std::to_string(expected)
          + " bytes expected for k = " + std::to_string(pre.k) + ", actual " + std::to_string(size));
    }
  }
  minhash_sketch_alloc sketch(pre.k, seed, allocator);
  if (pre.is_empty) { return sketch; }
  ptr += PREAMBLE_BYTES;
  for (uint64_t& slot: sketch.signature_) {
    slot = check_slot_value(load_u64(ptr));
    ptr += sizeof(uint64_t);
  }
  sketch.is_empty_ = false;
  return sketch;
}

template<typename A>
void minhash_sketch_alloc<A>::write_preamble(uint8_t* bytes) const {
  bytes[0] = minhash_constants::PREAMBLE_LONGS;
  bytes[1] = minhash_constants::SERIAL_VERSION;
  bytes[2] = minhash_constants::FAMILY_ID;
  bytes[3] = is_empty_ ? FLAG_EMPTY : static_cast<uint8_t>(0);
  store_u16(bytes + 4, static_cast<uint16_t>(signature_.size()));
  store_u16(bytes + 6, compute_seed_hash(seed_));
}

template<typename A>
auto minhash_sketch_alloc<A>::check_preamble(const uint8_t* bytes, uint64_t seed) -> preamble {
  if (bytes[0] != minhash_constants::PREAMBLE_LONGS) {
    throw std::invalid_argument("preamble longs: expected " + std::to_string(minhash_constants::PREAMBLE_LONGS)
        + ", actual " + std::to_string(bytes[0]));
  }
  if (bytes[1] != minhash_constants::SERIAL_VERSION) {
    throw std::invalid_argument("serial version: expected " + std::to_string(minhash_constants::SERIAL_VERSION)
        + ", actual " + std::to_string(bytes[1]));
  }
  if (bytes[2] != minhash_constants::FAMILY_ID) {
    throw std::invalid_argument("family ID: expected " + std::to_string(minhash_constants::FAMILY_ID)
        + ", actual " + std::to_string(bytes[2]));
  }
  const uint8_t flags = bytes[3];
  if ((flags & FLAG_HASH_LIST) != 0) {
    throw std::invalid_argument("flags: hash-list form is not supported by format version 1");
  }
  if ((flags & FLAGS_UNASSIGNED) != 0) {
    throw std::invalid_argument("flags: unassigned bits are set: " + std::to_string(flags));
  }
  const uint16_t k = load_u16(bytes + 4);
  if (k == 0) { throw std::invalid_argument("k: must be at least 1"); }
  const uint16_t seed_hash = load_u16(bytes + 6);
  const uint16_t expected_seed_hash = compute_seed_hash(seed);
  if (seed_hash != expected_seed_hash) {
    throw std::invalid_argument("seed hash: expected " + std::to_string(expected_seed_hash)
        + ", actual " + std::to_string(seed_hash));
  }
  preamble pre;
  pre.is_empty = (flags & FLAG_EMPTY) != 0;
  pre.k = k;
  return pre;
}

template<typename A>
void minhash_sketch_alloc<A>::store_u16(uint8_t* bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value & 0xFFU);
  bytes[1] = static_cast<uint8_t>(value >> 8U);
}

template<typename A>
void minhash_sketch_alloc<A>::store_u64(uint8_t* bytes, uint64_t value) {
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFU);
  }
}

template<typename A>
uint16_t minhash_sketch_alloc<A>::load_u16(const uint8_t* bytes) {
  return static_cast<uint16_t>(static_cast<unsigned>(bytes[0]) | (static_cast<unsigned>(bytes[1]) << 8U));
}

template<typename A>
uint64_t minhash_sketch_alloc<A>::load_u64(const uint8_t* bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
  }
  return value;
}

template<typename A>
uint64_t minhash_sketch_alloc<A>::check_slot_value(uint64_t value) {
  // slot values use 63 bits (ADR-0001)
  if (value > minhash_constants::EMPTY_MARKER) {
    throw std::invalid_argument("slot value: greater than INT64_MAX: " + std::to_string(value));
  }
  return value;
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
  if (k == 0) { throw std::invalid_argument("k must be at least 1"); }
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

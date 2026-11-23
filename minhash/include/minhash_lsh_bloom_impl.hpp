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

#ifndef MINHASH_LSH_BLOOM_IMPL_HPP_
#define MINHASH_LSH_BLOOM_IMPL_HPP_

#include <cmath>
#include <stdexcept>

namespace datasketches {

template<typename A>
minhash_lsh_bloom_alloc<A>::minhash_lsh_bloom_alloc(uint16_t num_bands, uint64_t max_distinct_items,
    double target_false_positive_prob, uint64_t seed, const A& allocator):
num_bands_(num_bands),
filters_(typename std::vector<filter_type, rebind_alloc<filter_type>>::allocator_type(allocator))
{
  if (num_bands == 0) { throw std::invalid_argument("num_bands must be at least 1"); }
  if (max_distinct_items == 0) { throw std::invalid_argument("max_distinct_items must be at least 1"); }
  // the negated comparisons also reject NaN
  if (!(target_false_positive_prob > 0.0) || !(target_false_positive_prob < 1.0)) {
    throw std::invalid_argument("target_false_positive_prob must be in (0, 1)");
  }
  // p = 1 - (1 - p_eff)^(1/b), in a form that keeps its precision for small p_eff
  const double per_band = -std::expm1(std::log1p(-target_false_positive_prob) / num_bands);
  const rebind_alloc<uint8_t> filter_alloc(allocator);
  filters_.reserve(num_bands);
  for (uint16_t band = 0; band < num_bands; ++band) {
    filters_.push_back(filter_type::builder::create_by_accuracy(max_distinct_items, per_band, seed, filter_alloc));
  }
}

template<typename A>
void minhash_lsh_bloom_alloc<A>::update(const sketch_type& sketch) {
  check_compatible(sketch);
  const auto keys = minhash_lsh_index_alloc<A>::band_keys(sketch, num_bands_);
  fix(sketch);
  for (uint16_t band = 0; band < num_bands_; ++band) {
    filters_[band].update(keys[band]);
  }
}

template<typename A>
bool minhash_lsh_bloom_alloc<A>::query(const sketch_type& sketch) const {
  check_compatible(sketch);
  const auto keys = minhash_lsh_index_alloc<A>::band_keys(sketch, num_bands_);
  for (uint16_t band = 0; band < num_bands_; ++band) {
    if (filters_[band].query(keys[band])) { return true; } // nothing changes, so stopping is safe
  }
  return false;
}

template<typename A>
bool minhash_lsh_bloom_alloc<A>::query_and_update(const sketch_type& sketch) {
  check_compatible(sketch);
  const auto keys = minhash_lsh_index_alloc<A>::band_keys(sketch, num_bands_);
  fix(sketch);
  // insert into every band, flagged or not, so later members of a chain are found
  bool flagged = false;
  for (uint16_t band = 0; band < num_bands_; ++band) {
    const bool present = filters_[band].query_and_update(keys[band]);
    flagged = flagged || present;
  }
  return flagged;
}

template<typename A>
uint64_t minhash_lsh_bloom_alloc<A>::get_capacity() const {
  uint64_t bits = 0;
  for (const auto& filter: filters_) { bits += filter.get_capacity(); }
  return bits;
}

template<typename A>
void minhash_lsh_bloom_alloc<A>::check_compatible(const sketch_type& sketch) const {
  if (!is_fixed_) { return; }
  if (sketch.get_num_hashes() != k_ || sketch.get_seed() != sketch_seed_) {
    throw std::invalid_argument("sketch is incompatible with the filters: k and seed must match the first sketch");
  }
}

template<typename A>
void minhash_lsh_bloom_alloc<A>::fix(const sketch_type& sketch) {
  if (is_fixed_) { return; }
  k_ = sketch.get_num_hashes();
  sketch_seed_ = sketch.get_seed();
  is_fixed_ = true;
}

} /* namespace datasketches */

#endif

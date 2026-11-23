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

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch.hpp>

#include "minhash_lsh_bloom.hpp"

namespace datasketches {

namespace {

minhash_sketch make_sketch(uint64_t from, uint64_t to, uint16_t k = 16, uint64_t seed = DEFAULT_SEED) {
  auto sketch = minhash_sketch::builder().set_k(k).set_seed(seed).build();
  for (uint64_t i = from; i < to; ++i) { sketch.update(i); }
  return sketch;
}

// The 200 documents of the MH-19 end-to-end check, k = 128. Documents 0 to 149 are
// [1000 d, 1000 d + 100). Documents 150 to 199 are 10 groups of 5: member m of group g is
// document g with the items [1000 g + 10 m, + 10) moved to [10^9 + 1000 g + 10 m, + 10).
std::vector<minhash_sketch> mh19_documents() {
  const uint64_t unrelated = 150;
  const uint64_t documents = 200;
  const uint64_t group_size = 5;
  std::vector<minhash_sketch> sketches;
  sketches.reserve(documents);
  for (uint64_t d = 0; d < documents; ++d) {
    auto s = minhash_sketch::builder().set_k(128).set_seed(DEFAULT_SEED).build();
    const bool member = d >= unrelated;
    const uint64_t base = member ? (d - unrelated) / group_size : d;
    const uint64_t moved = 1000 * base + (member ? 10 * ((d - unrelated) % group_size) : 0);
    for (uint64_t item = 1000 * base; item < 1000 * base + 100; ++item) {
      const bool is_moved = member && item >= moved && item < moved + 10;
      s.update(is_moved ? 1000000000 + item : item);
    }
    sketches.push_back(s);
  }
  return sketches;
}

// The MH-19 data with chains in place of the groups: member m of group g is the window
// [1000 g + 15 (m + 1), + 100), so neighbours in a group have J = 85/115 and the far ends
// have J = 25/175. With this data, a query_and_update that stops at the first band hit
// gives wrong flags (the MH-19 data alone does not show it).
std::vector<minhash_sketch> chain_documents() {
  const uint64_t unrelated = 150;
  const uint64_t documents = 200;
  const uint64_t group_size = 5;
  const uint64_t step = 15;
  std::vector<minhash_sketch> sketches;
  sketches.reserve(documents);
  for (uint64_t d = 0; d < documents; ++d) {
    auto s = minhash_sketch::builder().set_k(128).set_seed(DEFAULT_SEED).build();
    const bool member = d >= unrelated;
    const uint64_t base = member ? (d - unrelated) / group_size : d;
    const uint64_t shift = member ? step * ((d - unrelated) % group_size + 1) : 0;
    for (uint64_t item = 1000 * base + shift; item < 1000 * base + shift + 100; ++item) { s.update(item); }
    sketches.push_back(s);
  }
  return sketches;
}

// The band-key rule: document d is flagged when an earlier document e (flagged or not)
// has the same band key as d in at least one band.
std::vector<bool> reference_flags(const std::vector<minhash_sketch>& sketches, uint16_t num_bands) {
  std::vector<minhash_lsh_index::vector_u64> keys;
  keys.reserve(sketches.size());
  for (const auto& sketch: sketches) { keys.push_back(minhash_lsh_index::band_keys(sketch, num_bands)); }
  std::vector<bool> flags(sketches.size(), false);
  for (size_t d = 0; d < sketches.size(); ++d) {
    for (size_t e = 0; e < d && !flags[d]; ++e) {
      for (uint16_t band = 0; band < num_bands; ++band) {
        if (keys[d][band] == keys[e][band]) { flags[d] = true; break; }
      }
    }
  }
  return flags;
}

} // namespace

TEST_CASE("minhash lsh bloom: invalid constructor arguments", "[minhash]") {
  REQUIRE_THROWS_AS(minhash_lsh_bloom(0, 100, 0.01), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_bloom(4, 0, 0.01), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_bloom(4, 100, 0.0), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_bloom(4, 100, 1.0), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_bloom(4, 100, -0.1), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_bloom(4, 100, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
  REQUIRE_NOTHROW(minhash_lsh_bloom(4, 100, 0.01));
}

TEST_CASE("minhash lsh bloom: query_and_update flags the second copy", "[minhash]") {
  minhash_lsh_bloom bloom(4, 100, 1e-6);
  const auto a = make_sketch(0, 100);
  REQUIRE_FALSE(bloom.query_and_update(a));
  REQUIRE(bloom.query_and_update(a));
}

TEST_CASE("minhash lsh bloom: query changes nothing", "[minhash]") {
  minhash_lsh_bloom bloom(4, 100, 1e-6);
  const auto a = make_sketch(0, 100);
  REQUIRE_FALSE(bloom.query(a));
  REQUIRE_FALSE(bloom.query(a));
  bloom.update(a);
  REQUIRE(bloom.query(a));
  REQUIRE_FALSE(bloom.query(make_sketch(1000, 1100))); // disjoint items share no band key
}

TEST_CASE("minhash lsh bloom: invalid sketches", "[minhash]") {
  minhash_lsh_bloom bloom(4, 100, 1e-6);
  const auto empty = minhash_sketch::builder().set_k(16).build();
  REQUIRE_THROWS_AS(bloom.update(empty), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query(empty), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query_and_update(empty), std::invalid_argument);

  // 4 does not divide 6
  REQUIRE_THROWS_AS(bloom.update(make_sketch(0, 10, 6)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query(make_sketch(0, 10, 6)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query_and_update(make_sketch(0, 10, 6)), std::invalid_argument);

  // the first update fixes k = 16 and the sketch seed
  bloom.update(make_sketch(0, 10, 16));
  REQUIRE_THROWS_AS(bloom.update(make_sketch(0, 10, 8)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query(make_sketch(0, 10, 8)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query_and_update(make_sketch(0, 10, 8)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.update(make_sketch(0, 10, 16, 1)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query(make_sketch(0, 10, 16, 1)), std::invalid_argument);
  REQUIRE_THROWS_AS(bloom.query_and_update(make_sketch(0, 10, 16, 1)), std::invalid_argument);
  REQUIRE(bloom.query(make_sketch(0, 10, 16)));
}

TEST_CASE("minhash lsh bloom: a sketch that throws does not fix k", "[minhash]") {
  {
    minhash_lsh_bloom bloom(4, 100, 1e-6);
    REQUIRE_THROWS_AS(bloom.update(minhash_sketch::builder().set_k(16).build()), std::invalid_argument);
    REQUIRE_THROWS_AS(bloom.query_and_update(make_sketch(0, 10, 6)), std::invalid_argument);
    REQUIRE_FALSE(bloom.query_and_update(make_sketch(0, 10, 8)));
    REQUIRE_THROWS_AS(bloom.update(make_sketch(0, 10, 16)), std::invalid_argument);
  }
  {
    // query on a fresh object does not fix k or the seed
    minhash_lsh_bloom bloom(4, 100, 1e-6);
    REQUIRE_FALSE(bloom.query(make_sketch(0, 10, 16)));
    REQUIRE_FALSE(bloom.query(make_sketch(0, 10, 8, 1)));
    bloom.update(make_sketch(0, 10, 12, 2));
    REQUIRE(bloom.query(make_sketch(0, 10, 12, 2)));
  }
}

TEST_CASE("minhash lsh bloom: capacity", "[minhash]") {
  for (const uint16_t b: {1, 4, 16, 32}) {
    for (const double p_eff: {0.01, 1e-9}) {
      const uint64_t n = 200;
      const minhash_lsh_bloom bloom(b, n, p_eff);
      const double p = -std::expm1(std::log1p(-p_eff) / b);
      const uint64_t bits = bloom_filter::builder::suggest_num_filter_bits(n, p);
      const uint64_t rounded = (bits + 63) / 64 * 64; // the Bloom filter rounds up to a multiple of 64
      INFO("b = " << b << ", p_eff = " << p_eff);
      REQUIRE(bloom.get_capacity() == b * rounded);
    }
  }
}

TEST_CASE("minhash lsh bloom: flags equal the band-key rule", "[minhash]") {
  // A Bloom filter adds a false flag with a chance of about 200 * 1e-9, and the seeds
  // are fixed, so the flags equal the band-key rule on every run.
  const std::vector<std::vector<minhash_sketch>> data_sets = {mh19_documents(), chain_documents()};
  for (size_t data = 0; data < data_sets.size(); ++data) {
    const auto& sketches = data_sets[data];
    for (const uint16_t b: {16, 32}) {
      const auto expected = reference_flags(sketches, b);
      minhash_lsh_bloom bloom(b, 200, 1e-9);
      size_t num_flagged = 0;
      for (size_t d = 0; d < sketches.size(); ++d) {
        INFO("data " << (data == 0 ? "MH-19" : "chains") << ", b = " << b << ", document " << d);
        const bool flagged = bloom.query_and_update(sketches[d]);
        REQUIRE(flagged == expected[d]);
        if (flagged) { ++num_flagged; }
      }
      INFO("data " << (data == 0 ? "MH-19" : "chains") << ", b = " << b << ", flagged " << num_flagged);
      REQUIRE(num_flagged > 0); // the data has near-copies
    }
  }
}

} /* namespace datasketches */

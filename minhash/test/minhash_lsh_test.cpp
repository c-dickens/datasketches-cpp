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

#include <catch2/catch.hpp>

#include <test_allocator.hpp>

#include "minhash_lsh.hpp"

namespace datasketches {

static minhash_sketch make_sketch(uint64_t from, uint64_t to, uint16_t k = 112,
    uint64_t seed = DEFAULT_SEED) {
  auto sketch = minhash_sketch::builder().set_k(k).set_seed(seed).build();
  for (uint64_t i = from; i < to; ++i) sketch.update(i);
  return sketch;
}

TEST_CASE("minhash lsh: band keys", "[minhash_lsh]") {
  const auto sketch_a = make_sketch(0, 1000);
  const auto sketch_b = make_sketch(0, 1000);
  const auto sketch_c = make_sketch(0, 1000, 112, 123); // different seed

  // deterministic, identical sketches produce identical keys, one key per band
  const auto keys_a = minhash_lsh_index::band_keys(sketch_a, 14);
  REQUIRE(keys_a.size() == 14);
  REQUIRE(minhash_lsh_index::band_keys(sketch_a, 14) == keys_a);
  REQUIRE(minhash_lsh_index::band_keys(sketch_b, 14) == keys_a);
  REQUIRE(minhash_lsh_index::band_keys(sketch_c, 14) != keys_a);

  // num_bands validation: 0, non-divisor of k = 112, larger than k
  REQUIRE_THROWS_AS(minhash_lsh_index::band_keys(sketch_a, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index::band_keys(sketch_a, 5), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index::band_keys(sketch_a, 224), std::invalid_argument);

  // empty sketch
  const auto empty = minhash_sketch::builder().set_k(112).build();
  REQUIRE_THROWS_AS(minhash_lsh_index::band_keys(empty, 14), std::invalid_argument);
}

TEST_CASE("minhash lsh: band covers exactly its slot range", "[minhash_lsh]") {
  // reproduce the keys independently, pinning both the slice bounds and the seed ^ band
  // derivation: a consistent off-by-one in the slicing would survive the determinism tests
  const auto sketch = make_sketch(0, 200, 16);
  const auto& signature = sketch.get_signature();

  for (const uint16_t num_bands: {1, 2, 4, 16}) {
    const uint32_t rows = 16 / num_bands;
    const auto keys = minhash_lsh_index::band_keys(sketch, num_bands);
    REQUIRE(keys.size() == num_bands);
    for (uint16_t band = 0; band < num_bands; ++band) {
      HashState expected;
      MurmurHash3_x64_128(signature.data() + band * rows, rows * sizeof(uint64_t),
          sketch.get_seed() ^ band, expected);
      REQUIRE(keys[band] == expected.h1);
    }
  }
}

TEST_CASE("minhash lsh: index validation", "[minhash_lsh]") {
  REQUIRE_THROWS_AS(minhash_lsh_index(0, 0.5), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index(14, 0.0), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index(14, -0.1), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index(14, 1.5), std::invalid_argument);
  REQUIRE_THROWS_AS(minhash_lsh_index(14, std::numeric_limits<double>::quiet_NaN()),
      std::invalid_argument);
  minhash_lsh_index ok(14, 1.0); // threshold 1.0 is valid
  REQUIRE(ok.get_num_documents() == 0);
  REQUIRE(ok.get_duplicate_clusters().empty());

  minhash_lsh_index index(14, 0.8);
  index.add(1, make_sketch(0, 1000));
  // duplicate id
  REQUIRE_THROWS_AS(index.add(1, make_sketch(0, 1000)), std::invalid_argument);
  // mismatched k and seed on a later add
  REQUIRE_THROWS_AS(index.add(2, make_sketch(0, 1000, 224)), std::invalid_argument);
  REQUIRE_THROWS_AS(index.add(2, make_sketch(0, 1000, 112, 123)), std::invalid_argument);
  // empty sketch
  const auto empty = minhash_sketch::builder().set_k(112).build();
  REQUIRE_THROWS_AS(index.add(2, empty), std::invalid_argument);
  // k not divisible by num_bands
  minhash_lsh_index index6(6, 0.8);
  REQUIRE_THROWS_AS(index6.add(1, make_sketch(0, 1000)), std::invalid_argument);
  // query with mismatched k after adds
  REQUIRE_THROWS_AS(index.query_candidates(make_sketch(0, 1000, 224)), std::invalid_argument);
}

TEST_CASE("minhash lsh: exact duplicates cluster", "[minhash_lsh]") {
  minhash_lsh_index index(14, 0.9);
  index.add(1, make_sketch(0, 500));
  index.add(2, make_sketch(0, 500));      // identical content
  index.add(3, make_sketch(10000, 10500)); // unrelated
  REQUIRE(index.get_num_documents() == 3);

  const auto candidates = index.query_candidates(make_sketch(0, 500));
  REQUIRE(candidates == minhash_lsh_index::vector_u64{1, 2});

  const auto clusters = index.get_duplicate_clusters();
  REQUIRE(clusters.size() == 1);
  REQUIRE(clusters[0] == minhash_lsh_index::vector_u64{1, 2});
}

TEST_CASE("minhash lsh: s-curve statistics at 14 bands x 8 rows", "[minhash_lsh]") {
  // P(candidate | J) = 1 - (1 - J^8)^14: ~0.9996 at J = 0.9, ~4e-5 at J = 0.2
  const int num_trials = 40;
  int high_candidates = 0;
  int low_candidates = 0;
  for (int trial = 0; trial < num_trials; ++trial) {
    const uint64_t base = static_cast<uint64_t>(trial) * 1000000;
    const uint64_t seed = DEFAULT_SEED + trial;
    auto make = [&](uint64_t from, uint64_t to, uint64_t shared_to) {
      auto sketch = minhash_sketch::builder().set_k(112).set_seed(seed).build();
      for (uint64_t i = base; i < base + shared_to; ++i) sketch.update(i);
      for (uint64_t i = base + from; i < base + to; ++i) sketch.update(i);
      return sketch;
    };
    // J = 900 / 1000 = 0.9: 900 shared, 50 exclusive each
    const auto high_a = make(10000, 10050, 900);
    const auto high_b = make(20000, 20050, 900);
    const auto keys_high_a = minhash_lsh_index::band_keys(high_a, 14);
    const auto keys_high_b = minhash_lsh_index::band_keys(high_b, 14);
    for (int band = 0; band < 14; ++band) {
      if (keys_high_a[band] == keys_high_b[band]) { ++high_candidates; break; }
    }
    // J = 200 / 1000 = 0.2: 200 shared, 400 exclusive each
    const auto low_a = make(10000, 10400, 200);
    const auto low_b = make(20000, 20400, 200);
    const auto keys_low_a = minhash_lsh_index::band_keys(low_a, 14);
    const auto keys_low_b = minhash_lsh_index::band_keys(low_b, 14);
    for (int band = 0; band < 14; ++band) {
      if (keys_low_a[band] == keys_low_b[band]) { ++low_candidates; break; }
    }
  }
  REQUIRE(high_candidates >= 36); // expected ~40
  REQUIRE(low_candidates <= 3);   // expected ~0
}

TEST_CASE("minhash lsh: verification filters chance band collisions", "[minhash_lsh]") {
  // r = 1 rows per band makes band collisions common at moderate similarity,
  // so candidates appear that verification must then reject
  minhash_lsh_index index(8, 0.99);
  index.add(1, make_sketch(0, 1000, 8));
  index.add(2, make_sketch(500, 1500, 8)); // true J = 1/3

  const auto candidates = index.query_candidates(make_sketch(500, 1500, 8));
  REQUIRE(candidates.size() == 2); // both docs share a band with the query
  REQUIRE(index.get_duplicate_clusters().empty()); // but neither pair verifies at 0.99
}

TEST_CASE("minhash lsh: chain clustering is a transitive closure", "[minhash_lsh]") {
  // A ~ B and B ~ C above the threshold, A ~ C below it: one cluster {A, B, C}
  const auto sketch_a = make_sketch(0, 1000);
  const auto sketch_b = make_sketch(143, 1143);
  const auto sketch_c = make_sketch(286, 1286);
  const auto sketch_d = make_sketch(5000, 6000); // unrelated
  const double threshold = 0.65;
  // true J(A,B) = J(B,C) = 857/1143 ~ 0.75, true J(A,C) = 714/1286 ~ 0.56
  REQUIRE(sketch_a.jaccard(sketch_b) >= threshold);
  REQUIRE(sketch_b.jaccard(sketch_c) >= threshold);
  REQUIRE(sketch_a.jaccard(sketch_c) < threshold);

  minhash_lsh_index index(28, threshold); // 28 bands x 4 rows: P(candidate | 0.75) ~ 1
  index.add(10, sketch_a);
  index.add(11, sketch_b);
  index.add(12, sketch_c);
  index.add(13, sketch_d);

  const auto clusters = index.get_duplicate_clusters();
  REQUIRE(clusters.size() == 1);
  REQUIRE(clusters[0] == minhash_lsh_index::vector_u64{10, 11, 12});
}

TEST_CASE("minhash lsh: allocator", "[minhash_lsh]") {
  using index_test_alloc = minhash_lsh_index_alloc<test_allocator<uint64_t>>;
  using sketch_test_alloc = minhash_sketch_alloc<test_allocator<uint64_t>>;
  test_allocator_total_bytes = 0;
  test_allocator_net_allocations = 0;
  {
    index_test_alloc index(4, 0.8, test_allocator<uint64_t>(0));
    for (uint64_t doc = 0; doc < 5; ++doc) {
      auto sketch = sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(16).build();
      for (uint64_t i = doc * 10; i < doc * 10 + 100; ++i) sketch.update(i);
      index.add(doc, sketch);
    }
    REQUIRE(index.get_num_documents() == 5);
    const auto clusters = index.get_duplicate_clusters();
    REQUIRE(!clusters.empty());
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

} /* namespace datasketches */

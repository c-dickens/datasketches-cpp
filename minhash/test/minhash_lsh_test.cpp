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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

#include <test_allocator.hpp>

#include "minhash_lsh.hpp"
#include "xxhash64.h"

namespace datasketches {

static minhash_sketch make_sketch(uint64_t from, uint64_t to, uint16_t k = 112,
    uint64_t seed = DEFAULT_SEED) {
  auto sketch = minhash_sketch::builder().set_k(k).set_seed(seed).build();
  for (uint64_t i = from; i < to; ++i) { sketch.update(i); }
  return sketch;
}

namespace {

// brute-force reference for get_duplicate_clusters(): the id of sketches[i] is i,
// candidate pairs share the key of one band, verified pairs reach the threshold,
// and the clusters are the connected components of the verified pairs
std::vector<std::vector<uint64_t>> reference_clusters(const std::vector<minhash_sketch>& sketches,
    uint16_t num_bands, double threshold) {
  std::vector<minhash_lsh_index::vector_u64> keys;
  keys.reserve(sketches.size());
  for (const auto& sketch: sketches) { keys.push_back(minhash_lsh_index::band_keys(sketch, num_bands)); }

  std::vector<size_t> parent(sketches.size());
  for (size_t i = 0; i < parent.size(); ++i) { parent[i] = i; }
  auto find = [&parent](size_t x) {
    while (parent[x] != x) { x = parent[x]; }
    return x;
  };

  for (size_t i = 0; i < sketches.size(); ++i) {
    for (size_t j = i + 1; j < sketches.size(); ++j) {
      bool candidate = false;
      for (uint16_t band = 0; band < num_bands; ++band) {
        if (keys[i][band] == keys[j][band]) { candidate = true; break; }
      }
      if (candidate && sketches[i].jaccard(sketches[j]) >= threshold) {
        const size_t root_i = find(i);
        const size_t root_j = find(j);
        if (root_i != root_j) { parent[std::max(root_i, root_j)] = std::min(root_i, root_j); }
      }
    }
  }

  // each root is the smallest id of its component, so ascending roots order the
  // clusters by their smallest id, and ascending ids keep each cluster sorted
  std::vector<std::vector<uint64_t>> components(sketches.size());
  for (size_t i = 0; i < sketches.size(); ++i) { components[find(i)].push_back(i); }
  std::vector<std::vector<uint64_t>> clusters;
  for (auto& component: components) {
    if (component.size() >= 2) { clusters.push_back(std::move(component)); }
  }
  return clusters;
}

// a sketch of the items in the half-open ranges [first, second)
minhash_sketch make_sketch_from_ranges(const std::vector<std::pair<uint64_t, uint64_t>>& ranges, uint16_t k) {
  auto sketch = minhash_sketch::builder().set_k(k).set_seed(DEFAULT_SEED).build();
  for (const auto& range: ranges) {
    for (uint64_t item = range.first; item < range.second; ++item) { sketch.update(item); }
  }
  return sketch;
}

} /* anonymous namespace */

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
  // reproduce the keys independently, pinning both the slice bounds and the seed + band
  // derivation: a consistent off-by-one in the slicing would survive the determinism tests
  const auto sketch = make_sketch(0, 200, 16);
  const auto& signature = sketch.get_signature();

  for (const uint16_t num_bands: {1, 2, 4, 16}) {
    const uint32_t rows = 16 / num_bands;
    const auto keys = minhash_lsh_index::band_keys(sketch, num_bands);
    REQUIRE(keys.size() == num_bands);
    for (uint16_t band = 0; band < num_bands; ++band) {
      REQUIRE(keys[band] == XXHash64::hash(signature.data() + band * rows, rows * sizeof(uint64_t),
          sketch.get_seed() + band));
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

TEST_CASE("minhash lsh: s-curve statistics at 14 bands x 8 rows", "[minhash_lsh][minhash][stats]") {
  // P(candidate | J) = 1 - (1 - J^8)^14: ~0.9996 at J = 0.9, ~4e-5 at J = 0.2
  // exact binomial tails over 40 trials: P(high < 36) ~ 5e-12 and P(low > 3) ~ 2e-13,
  // both below the 5 SD rate (~6e-7), so the thresholds stay as they are
  const int num_trials = 40;
  int high_candidates = 0;
  int low_candidates = 0;
  for (int trial = 0; trial < num_trials; ++trial) {
    const uint64_t base = static_cast<uint64_t>(trial) * 1000000;
    const uint64_t seed = DEFAULT_SEED + trial;
    auto make = [&](uint64_t from, uint64_t to, uint64_t shared_to) {
      auto sketch = minhash_sketch::builder().set_k(112).set_seed(seed).build();
      for (uint64_t i = base; i < base + shared_to; ++i) { sketch.update(i); }
      for (uint64_t i = base + from; i < base + to; ++i) { sketch.update(i); }
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
      for (uint64_t i = doc * 10; i < doc * 10 + 100; ++i) { sketch.update(i); }
      index.add(doc, sketch);
    }
    REQUIRE(index.get_num_documents() == 5);
    const auto clusters = index.get_duplicate_clusters();
    REQUIRE(!clusters.empty());
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

TEST_CASE("minhash lsh: identical sketches cluster in linear time", "[minhash_lsh]") {
  const uint64_t num_copies = 100000;
  const auto sketch = make_sketch(0, 100, 16);
  minhash_lsh_index index(4, 0.9);
  for (uint64_t id = 0; id < num_copies; ++id) { index.add(id, sketch); }

  const auto start = std::chrono::steady_clock::now();
  const auto clusters = index.get_duplicate_clusters();
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(clusters.size() == 1);
  REQUIRE(clusters[0].size() == num_copies);
  for (uint64_t id = 0; id < num_copies; ++id) { REQUIRE(clusters[0][id] == id); }
#ifdef NDEBUG
  REQUIRE(elapsed < std::chrono::seconds(1));
#else
  (void) elapsed; // the time is checked only in an optimised build
#endif
}

TEST_CASE("minhash lsh: clusters match the reference on planted near-duplicates", "[minhash_lsh]") {
  const uint16_t k = 112;
  const uint16_t num_bands = 14;
  const double threshold = 0.8;
  const uint64_t num_unrelated = 1500;
  const uint64_t num_planted_bases = 100;

  auto build = [&](const std::vector<std::pair<uint64_t, uint64_t>>& ranges) {
    return make_sketch_from_ranges(ranges, k);
  };

  std::vector<minhash_sketch> sketches;
  sketches.reserve(num_unrelated + 5 * num_planted_bases);
  for (uint64_t i = 0; i < num_unrelated; ++i) { sketches.push_back(build({{1000 * i, 1000 * i + 100}})); }
  for (uint64_t c = 0; c < num_planted_bases; ++c) {
    const uint64_t base = 1000 * c;
    // two copies identical to id c (J = 1)
    sketches.push_back(build({{base, base + 100}}));
    sketches.push_back(build({{base, base + 100}}));
    // two copies identical to each other, J = 99/101 with id c. Matches with id c ~ Binomial(k, J):
    // mean 109.8, SD sqrt(k J (1 - J)) = 1.47, and verification needs 90, about 13 SD below the mean.
    // P(no candidate) = (1 - J^8)^14 = 2.4e-12.
    sketches.push_back(build({{base + 1, base + 100}, {10000000 + base, 10000000 + base + 1}}));
    sketches.push_back(build({{base + 1, base + 100}, {10000000 + base, 10000000 + base + 1}}));
    // J = 75/125 = 0.6 with id c, below the threshold
    sketches.push_back(build({{base + 25, base + 100}, {20000000 + base, 20000000 + base + 25}}));
  }
  REQUIRE(sketches.size() == 2000);

  minhash_lsh_index index(num_bands, threshold);
  for (size_t i = 0; i < sketches.size(); ++i) { index.add(i, sketches[i]); }

  const auto expected = reference_clusters(sketches, num_bands, threshold);
  // ids c, 1500 + 5c and 1501 + 5c are identical, and disjoint item sets have J = 0
  REQUIRE(expected.size() == num_planted_bases);
  for (const auto& cluster: expected) { REQUIRE(cluster.size() >= 3); }

  const auto clusters = index.get_duplicate_clusters();
  REQUIRE(clusters.size() == expected.size());
  for (size_t i = 0; i < clusters.size(); ++i) {
    REQUIRE(std::vector<uint64_t>(clusters[i].begin(), clusters[i].end()) == expected[i]);
  }
}

} /* namespace datasketches */

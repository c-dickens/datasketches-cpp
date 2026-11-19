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
#include <vector>

#include <catch2/catch.hpp>

#include "minhash_sketch.hpp"

// Statistical tests of the MinHash sketch. Every trial t uses the seed
// DEFAULT_SEED + t, so the results are deterministic. Each expected value comes
// from theory, and each threshold allows 5 SD unless a comment says otherwise.
// For union size u and intersection size i, sketch B gets items [0, i) and
// sketch A gets items [0, u), so the union is u, the intersection is i and the
// true Jaccard similarity is J = i / u.

namespace datasketches {

namespace {

const uint16_t STATS_K = 128;

struct overlap_case {
  uint64_t union_size;
  uint64_t intersection_size;
  uint32_t trials;
};

// The 6 (u, i, T) combinations of tests 1 and 2.
const std::vector<overlap_case>& rate_cases() {
  static const std::vector<overlap_case> cases = {
    {10, 1, 2000}, {10, 5, 2000}, {10, 9, 2000},
    {1000, 100, 500}, {1000, 500, 500}, {1000, 900, 500}
  };
  return cases;
}

// Builds B = [0, i) and A = [0, u) with k slots and the given seed. A is a copy
// of B that also gets [i, u).
void build_pair(uint64_t union_size, uint64_t intersection_size, uint16_t k, uint64_t seed,
    minhash_sketch& sketch_a, minhash_sketch& sketch_b) {
  sketch_b = minhash_sketch::builder().set_k(k).set_seed(seed).build();
  for (uint64_t item = 0; item < intersection_size; ++item) { sketch_b.update(item); }
  sketch_a = sketch_b;
  for (uint64_t item = intersection_size; item < union_size; ++item) { sketch_a.update(item); }
}

uint32_t count_matches(const minhash_sketch& sketch_a, const minhash_sketch& sketch_b) {
  uint32_t matches = 0;
  const auto& sig_a = sketch_a.get_signature();
  const auto& sig_b = sketch_b.get_signature();
  for (size_t slot = 0; slot < sig_a.size(); ++slot) {
    if (sig_a[slot] == sig_b[slot]) { ++matches; }
  }
  return matches;
}

// The number of equal slots in each of `trials` trials, with k = STATS_K.
std::vector<uint32_t> match_counts(uint64_t union_size, uint64_t intersection_size, uint32_t trials) {
  std::vector<uint32_t> counts;
  counts.reserve(trials);
  auto sketch_a = minhash_sketch::builder().set_k(STATS_K).build();
  auto sketch_b = minhash_sketch::builder().set_k(STATS_K).build();
  for (uint32_t t = 0; t < trials; ++t) {
    build_pair(union_size, intersection_size, STATS_K, DEFAULT_SEED + t, sketch_a, sketch_b);
    counts.push_back(count_matches(sketch_a, sketch_b));
  }
  return counts;
}

} // namespace

TEST_CASE("minhash sketch stats: slot rate", "[minhash_sketch_stats][minhash][stats]") {
  for (const auto& c: rate_cases()) {
    const double j = static_cast<double>(c.intersection_size) / static_cast<double>(c.union_size);
    const auto counts = match_counts(c.union_size, c.intersection_size, c.trials);
    uint64_t total = 0;
    for (const uint32_t m: counts) { total += m; }
    // M = sum of matches over T trials ~ Binomial(T * k, J), so
    // mean = T * k * J and SD = sqrt(T * k * J * (1 - J)). Require |M - mean| <= 5 SD.
    const double n = static_cast<double>(c.trials) * STATS_K;
    const double mean = n * j;
    const double sd = std::sqrt(n * j * (1 - j));
    INFO("u = " << c.union_size << ", i = " << c.intersection_size << ", T = " << c.trials
        << ", M = " << total << ", expected " << mean << " +/- " << 5 * sd);
    REQUIRE(std::fabs(static_cast<double>(total) - mean) <= 5 * sd);
  }
}

TEST_CASE("minhash sketch stats: variance", "[minhash_sketch_stats][minhash][stats]") {
  for (const auto& c: rate_cases()) {
    const double j = static_cast<double>(c.intersection_size) / static_cast<double>(c.union_size);
    const auto counts = match_counts(c.union_size, c.intersection_size, c.trials);
    const auto trials = static_cast<double>(c.trials);
    double sum = 0;
    for (const uint32_t m: counts) { sum += static_cast<double>(m) / STATS_K; }
    const double mean = sum / trials;
    double sum_sq = 0;
    for (const uint32_t m: counts) {
      const double diff = static_cast<double>(m) / STATS_K - mean;
      sum_sq += diff * diff;
    }
    const double s2 = sum_sq / (trials - 1);
    // J_hat = matches / k with matches ~ Binomial(k, J), so sigma^2 = J (1 - J) / k.
    // (T - 1) s^2 / sigma^2 is approximately chi-square(T - 1). With the excess
    // kurtosis g = (1 - 6 J (1 - J)) / (k J (1 - J)) of the binomial,
    // Var(s^2) ~ sigma^4 (2 / (T - 1) + g / T). Require |s^2 - sigma^2| <= 5 SD.
    const double sigma2 = j * (1 - j) / STATS_K;
    const double g = (1 - 6 * j * (1 - j)) / (STATS_K * j * (1 - j));
    const double sd = sigma2 * std::sqrt(2 / (trials - 1) + g / trials);
    INFO("u = " << c.union_size << ", i = " << c.intersection_size << ", T = " << c.trials
        << ", s^2 = " << s2 << ", expected " << sigma2 << " +/- " << 5 * sd);
    REQUIRE(std::fabs(s2 - sigma2) <= 5 * sd);
  }
}

TEST_CASE("minhash sketch stats: bounds coverage", "[minhash_sketch_stats][minhash][stats]") {
  // Nominal coverage of +/- z SD under the normal distribution: erf(z / sqrt(2)).
  // 2 SD: 0.9545, 3 SD: 0.9973. Require covered / T >= nominal - 0.01.
  // With T = 15,000 the SD of the coverage rate at 2 SD is
  // sqrt(0.9545 * 0.0455 / 15000) = 0.0017, so 0.01 is 5.9 SD. The exact
  // coverage of the library bounds at k = 128 (binomial sum) is 0.975 / 0.998 at
  // J = 0.1 and 0.9, and 0.958 / 0.998 at J = 0.5, so each check has a margin.
  const uint64_t union_size = 10;
  const uint32_t trials = 15000;
  const double nominal2 = std::erf(2 / std::sqrt(2.0));
  const double nominal3 = std::erf(3 / std::sqrt(2.0));
  const std::vector<uint64_t> intersections = {1, 5, 9};
  auto sketch_a = minhash_sketch::builder().set_k(STATS_K).build();
  auto sketch_b = minhash_sketch::builder().set_k(STATS_K).build();
  for (const uint64_t intersection_size: intersections) {
    const double j = static_cast<double>(intersection_size) / static_cast<double>(union_size);
    uint32_t covered2 = 0;
    uint32_t covered3 = 0;
    for (uint32_t t = 0; t < trials; ++t) {
      build_pair(union_size, intersection_size, STATS_K, DEFAULT_SEED + t, sketch_a, sketch_b);
      const auto bounds2 = sketch_a.jaccard_bounds(sketch_b, 2);
      const auto bounds3 = sketch_a.jaccard_bounds(sketch_b, 3);
      if (bounds2[0] <= j && j <= bounds2[2]) { ++covered2; }
      if (bounds3[0] <= j && j <= bounds3[2]) { ++covered3; }
    }
    const double rate2 = static_cast<double>(covered2) / trials;
    const double rate3 = static_cast<double>(covered3) / trials;
    INFO("J = " << j << ", coverage 2 SD = " << rate2 << ", 3 SD = " << rate3);
    REQUIRE(rate2 >= nominal2 - 0.01);
    REQUIRE(rate3 >= nominal3 - 0.01);
  }
}

TEST_CASE("minhash sketch stats: band collision rate", "[minhash_sketch_stats][minhash][stats]") {
  // A = [0, 10), B = [0, 2), J = 0.2, k = 128, 32 bands of 4 consecutive slots.
  // If the slots of a band are independent, a band collides with probability
  // J^4 = 0.0016. C = colliding bands over T * 32 = 800,000 band observations
  // ~ Binomial(800000, 0.0016): mean 1280, SD 35.8. Require C in J^4 +/- 15%,
  // that is [1088, 1472], which is +/- 5.4 SD. Double hashing gives about 2x.
  const uint64_t union_size = 10;
  const uint64_t intersection_size = 2;
  const uint32_t trials = 25000;
  const size_t band_size = 4;
  const size_t num_bands = STATS_K / band_size;
  auto sketch_a = minhash_sketch::builder().set_k(STATS_K).build();
  auto sketch_b = minhash_sketch::builder().set_k(STATS_K).build();
  uint64_t collisions = 0;
  for (uint32_t t = 0; t < trials; ++t) {
    build_pair(union_size, intersection_size, STATS_K, DEFAULT_SEED + t, sketch_a, sketch_b);
    const auto& sig_a = sketch_a.get_signature();
    const auto& sig_b = sketch_b.get_signature();
    for (size_t band = 0; band < num_bands; ++band) {
      bool equal = true;
      for (size_t slot = band * band_size; slot < (band + 1) * band_size; ++slot) {
        if (sig_a[slot] != sig_b[slot]) { equal = false; break; }
      }
      if (equal) { ++collisions; }
    }
  }
  const double expected = static_cast<double>(trials) * num_bands * std::pow(0.2, 4);
  INFO("C = " << collisions << ", expected " << expected << ", ratio " << collisions / expected);
  REQUIRE(collisions >= 1088);
  REQUIRE(collisions <= 1472);
}

TEST_CASE("minhash sketch stats: merge equals single pass", "[minhash_sketch_stats][minhash][stats]") {
  // A = [0, 600), B = [400, 1000), U = [0, 1000). A merge is the element-wise
  // minimum, so it must equal the single-pass sketch exactly, in either order.
  const std::vector<uint16_t> k_values = {1, 7, 128};
  for (const uint16_t k: k_values) {
    for (uint64_t t = 0; t < 100; ++t) {
      const uint64_t seed = DEFAULT_SEED + t;
      auto sketch_a = minhash_sketch::builder().set_k(k).set_seed(seed).build();
      auto sketch_b = minhash_sketch::builder().set_k(k).set_seed(seed).build();
      auto sketch_u = minhash_sketch::builder().set_k(k).set_seed(seed).build();
      for (uint64_t item = 0; item < 600; ++item) { sketch_a.update(item); }
      for (uint64_t item = 400; item < 1000; ++item) { sketch_b.update(item); }
      for (uint64_t item = 0; item < 1000; ++item) { sketch_u.update(item); }
      INFO("k = " << k << ", t = " << t);
      auto a_then_b = sketch_a;
      a_then_b.merge(sketch_b);
      REQUIRE(a_then_b.get_signature() == sketch_u.get_signature());
      auto b_then_a = sketch_b;
      b_then_a.merge(sketch_a);
      REQUIRE(b_then_a.get_signature() == sketch_u.get_signature());
    }
  }
}

} /* namespace datasketches */

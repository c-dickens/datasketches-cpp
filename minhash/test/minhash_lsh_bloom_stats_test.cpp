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

#include "minhash_lsh_bloom.hpp"

// Statistical tests of minhash_lsh_bloom. The seeds are fixed, so the results are
// deterministic. Each expected value comes from theory, and each threshold allows 5 SD.

namespace datasketches {

namespace {

// document d has the 20 items [10^6 d, 10^6 d + 20)
minhash_sketch make_document(uint64_t d, uint16_t k) {
  auto sketch = minhash_sketch::builder().set_k(k).build();
  for (uint64_t item = 1000000 * d; item < 1000000 * d + 20; ++item) { sketch.update(item); }
  return sketch;
}

} // namespace

TEST_CASE("minhash lsh bloom stats: flag rate follows the LSH curve", "[minhash][stats]") {
  // k = 128, b = 32, r = 4. A = [0, 200), B = [0, i), J = i / 200. In trial t the sketches
  // use the seed DEFAULT_SEED + t. A fresh object gets update(A), then query(B).
  // B is flagged when one of the 32 bands collides: P = 1 - (1 - J^4)^32, that is about
  // 0.229, 0.564 and 0.873 for J = 0.3, 0.4 and 0.5. The Bloom part (p_eff = 1e-9) is
  // ignored. X ~ Binomial(2000, P); require |X - nP| <= 5 SD. Three checks at about
  // 6e-7 each keep the total false failure rate below 2e-6.
  const uint16_t k = 128;
  const uint16_t b = 32;
  const uint64_t union_size = 200;
  const uint32_t trials = 2000;
  for (const uint64_t intersection_size: {60, 80, 100}) {
    const double j = static_cast<double>(intersection_size) / union_size;
    const double p = 1.0 - std::pow(1.0 - std::pow(j, 4), b);
    uint32_t flagged = 0;
    for (uint32_t t = 0; t < trials; ++t) {
      auto sketch_b = minhash_sketch::builder().set_k(k).set_seed(DEFAULT_SEED + t).build();
      for (uint64_t item = 0; item < intersection_size; ++item) { sketch_b.update(item); }
      auto sketch_a = sketch_b;
      for (uint64_t item = intersection_size; item < union_size; ++item) { sketch_a.update(item); }
      minhash_lsh_bloom bloom(b, 16, 1e-9);
      bloom.update(sketch_a);
      if (bloom.query(sketch_b)) { ++flagged; }
    }
    const double mean = trials * p;
    const double sd = std::sqrt(trials * p * (1.0 - p));
    INFO("J = " << j << ", X = " << flagged << ", rate " << static_cast<double>(flagged) / trials
        << ", expected " << p);
    REQUIRE(std::fabs(flagged - mean) <= 5.0 * sd);
  }
}

TEST_CASE("minhash lsh bloom stats: Bloom error rate meets the target", "[minhash][stats]") {
  // k = 64, b = 16, max_distinct_items = 5000, p_eff = 0.01. Document d has the 20 items
  // [10^6 d, 10^6 d + 20). update() gets documents 0 to 4999, and query() gets the
  // 20,000 disjoint documents 5000 to 24999. Their band keys match nothing (equal keys
  // need r = 4 equal 63-bit slot values), so every flag is a Bloom false positive.
  // X ~ Binomial(20000, p_eff) at most: mean 200, SD 14.07. Require X <= 200 + 5 SD
  // (about 270), and X >= 50 = N p_eff / 4, which catches filters that are much too large.
  const uint16_t k = 64;
  const uint16_t b = 16;
  const uint64_t inserted = 5000;
  const uint64_t queried = 20000;
  const double p_eff = 0.01;
  minhash_lsh_bloom bloom(b, inserted, p_eff);
  for (uint64_t d = 0; d < inserted; ++d) { bloom.update(make_document(d, k)); }
  uint64_t flagged = 0;
  for (uint64_t d = inserted; d < inserted + queried; ++d) {
    if (bloom.query(make_document(d, k))) { ++flagged; }
  }
  const auto n = static_cast<double>(queried);
  const double upper = n * p_eff + 5.0 * std::sqrt(n * p_eff * (1.0 - p_eff));
  INFO("X = " << flagged << ", rate " << flagged / n << ", target " << p_eff << ", upper " << upper);
  REQUIRE(static_cast<double>(flagged) <= upper);
  REQUIRE(static_cast<double>(flagged) >= n * p_eff / 4.0);
}

} /* namespace datasketches */

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

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "minhash_lsh.hpp"
#include "xxhash64.h"

// REQUIRE(!x) instead of REQUIRE_FALSE(x): the Catch2 v2 flags of REQUIRE_FALSE make
// clang-analyzer-optin.core.EnumCastOutOfRange report a finding inside catch.hpp.

namespace {

using pair_list = std::vector<std::pair<std::size_t, std::size_t>>;

// A pair at exact J with the given error. Its bounds cover J when `covered` is true.
demo::pair_estimate hand_pair(double j, double error, bool covered = true) {
  const double est = j + error;
  const std::array<double, 3> hit{j - 0.5, est, j + 0.5};
  const std::array<double, 3> miss{j + 0.25, est, j + 0.5};
  return demo::pair_estimate{j, est, covered ? hit : miss, hit};
}

// n pairs at J = 0.5, half with error +e and half with error -e.
std::vector<demo::pair_estimate> symmetric_pairs(std::size_t n, double e) {
  std::vector<demo::pair_estimate> pairs;
  for (std::size_t i = 0; i < n; ++i) {
    pairs.push_back(hand_pair(0.5, i % 2 == 0 ? e : -e));
  }
  return pairs;
}

}  // namespace

TEST_CASE("jaccard_bin is floor(10 J), with J = 1 in bin 9", "[demo]") {
  REQUIRE(demo::jaccard_bin(0.0) == 0);
  REQUIRE(demo::jaccard_bin(0.09) == 0);
  REQUIRE(demo::jaccard_bin(0.1) == 1);
  REQUIRE(demo::jaccard_bin(0.95) == 9);
  REQUIRE(demo::jaccard_bin(1.0) == 9);
}

TEST_CASE("sample_indices is a fixed, sorted sample of distinct indices", "[demo]") {
  const auto a = demo::sample_indices(1000, 100, 1);
  REQUIRE(a == demo::sample_indices(1000, 100, 1));
  REQUIRE(a.size() == 100);
  REQUIRE(std::is_sorted(a.begin(), a.end()));
  REQUIRE(std::set<std::size_t>(a.begin(), a.end()).size() == 100);
  REQUIRE(a.back() < 1000);
  REQUIRE(a != demo::sample_indices(1000, 100, 2));
  REQUIRE(demo::sample_indices(5, 5, 1) == std::vector<std::size_t>{0, 1, 2, 3, 4});
  REQUIRE(demo::sample_indices(5, 0, 1).empty());
  REQUIRE_THROWS_AS(demo::sample_indices(5, 6, 1), std::invalid_argument);
}

TEST_CASE("check_pairs joins the sample pairs and the pairs of equal keys", "[demo]") {
  const std::vector<std::string> keys{"a", "", "a", "b", ""};
  // Empty keys do not pair up, and "b" has no partner.
  REQUIRE(demo::check_pairs(keys, 0, 1) == pair_list{{0, 2}});
  // A sample of all 5 gives the 10 pairs of 5 items. (0, 2) is in both parts, and appears once.
  const auto all = demo::check_pairs(keys, 5, 1);
  REQUIRE(all.size() == 10);
  REQUIRE(std::is_sorted(all.begin(), all.end()));
  REQUIRE(std::adjacent_find(all.begin(), all.end()) == all.end());
  for (const auto& p : all) {
    REQUIRE(p.first < p.second);
  }
}

TEST_CASE("sketch_of gives equal signatures for equal item lists", "[demo]") {
  const std::vector<std::string> items{"one", "two", "three"};
  const auto a = demo::sketch_of(items, 64);
  REQUIRE(a.get_num_hashes() == 64);
  REQUIRE(!a.is_empty());
  REQUIRE(a.get_signature() == demo::sketch_of(items, 64).get_signature());
  REQUIRE(demo::sketch_of({}, 64).is_empty());
}

TEST_CASE("summarise_bins computes bias, spread and coverage per bin", "[demo]") {
  // 1,000 pairs at J = 0.5 and k = 64, errors +0.0625 and -0.0625 (500 each).
  // mean bias = 0.
  // observed SD = sqrt(1000 * 0.0625^2 / 999) = 0.0625 * sqrt(1000 / 999) = 0.062531.
  // expected SD = sqrt(0.5 * 0.5 / 64) = 0.5 / 8 = 0.0625. The ratio is 1.0005: spread PASS.
  const auto bins = demo::summarise_bins(symmetric_pairs(1000, 0.0625), 64);
  REQUIRE(bins.size() == demo::NUM_BINS);
  const auto& b = bins[5];
  REQUIRE(b.bin == 5);
  REQUIRE(b.pairs == 1000);
  REQUIRE(b.checked);
  REQUIRE(b.mean_bias == Approx(0.0).margin(1e-12));
  REQUIRE(b.observed_sd == Approx(0.0625 * std::sqrt(1000.0 / 999.0)));
  REQUIRE(b.expected_sd == Approx(0.0625));
  REQUIRE(b.bias_pass);
  REQUIRE(b.spread_pass);
  REQUIRE(b.coverage2 == 1.0);
  REQUIRE(b.coverage2_pass);
  REQUIRE(b.coverage3_pass);
  REQUIRE(demo::bin_passes(b));
  for (std::size_t i = 0; i < demo::NUM_BINS; ++i) {
    if (i != 5) {
      REQUIRE(bins[i].pairs == 0);
      REQUIRE(!bins[i].checked);
    }
  }

  SECTION("every error shifted by +0.02 gives mean bias 0.02 > 0.01: bias FAIL") {
    auto pairs = symmetric_pairs(1000, 0.0625);
    for (auto& p : pairs) {
      p.estimate += 0.02;
    }
    const auto shifted = demo::summarise_bins(pairs, 64)[5];
    REQUIRE(shifted.mean_bias == Approx(0.02));
    REQUIRE(!shifted.bias_pass);
    REQUIRE(shifted.spread_pass);  // a shift does not change the SD
    REQUIRE(!demo::bin_passes(shifted));
  }

  SECTION("errors of +-0.1 give observed SD 0.1 * sqrt(1000 / 999) = 0.10005, ratio 1.6: spread FAIL") {
    const auto wide = demo::summarise_bins(symmetric_pairs(1000, 0.1), 64)[5];
    REQUIRE(wide.observed_sd / wide.expected_sd == Approx(1.6 * std::sqrt(1000.0 / 999.0)));
    REQUIRE(wide.bias_pass);
    REQUIRE(!wide.spread_pass);
  }

  SECTION("999 pairs: the bin is reported, not checked") {
    auto pairs = symmetric_pairs(999, 0.1);
    const auto small = demo::summarise_bins(pairs, 64)[5];
    REQUIRE(small.pairs == 999);
    REQUIRE(!small.checked);
    REQUIRE(demo::bin_passes(small));  // a failing figure does not fail an unchecked bin
  }

  SECTION("pairs at J = 0 and J = 1 are left out") {
    auto pairs = symmetric_pairs(1000, 0.0625);
    pairs.push_back(hand_pair(0.0, 0.3));
    pairs.push_back(hand_pair(1.0, -0.3));
    const auto with_ends = demo::summarise_bins(pairs, 64);
    REQUIRE(with_ends[0].pairs == 0);
    REQUIRE(with_ends[9].pairs == 0);
    REQUIRE(with_ends[5].pairs == 1000);
  }

  SECTION("coverage: 950 of 1,000 covered is 0.95 >= 0.9445 PASS; 940 is 0.94 < 0.9445 FAIL") {
    const std::array<std::size_t, 2> covered_counts{950, 940};
    for (const std::size_t covered : covered_counts) {
      std::vector<demo::pair_estimate> pairs;
      for (std::size_t i = 0; i < 1000; ++i) {
        pairs.push_back(hand_pair(0.5, i % 2 == 0 ? 0.0625 : -0.0625, i < covered));
      }
      const auto c = demo::summarise_bins(pairs, 64)[5];
      REQUIRE(c.coverage2 == Approx(static_cast<double>(covered) / 1000.0));
      REQUIRE(c.coverage2_pass == (covered == 950));
      REQUIRE(c.coverage3_pass);
    }
  }
}

TEST_CASE("bins_table has a header, a separator and one row per bin", "[demo]") {
  const auto table = demo::bins_table(demo::summarise_bins(symmetric_pairs(1000, 0.0625), 64));
  REQUIRE(std::count(table.begin(), table.end(), '\n') == 2 + static_cast<std::ptrdiff_t>(demo::NUM_BINS));
  // The SD ratio is sqrt(1000 / 999) = 1.0005, which rounds to 1.001.
  REQUIRE(table.find("| [0.5, 0.6) | 1000 | 0.00000 | 0.06253 | 0.06250 | 1.001 | 1.0000 | 1.0000 "
                     "| PASS | PASS | PASS | PASS |") != std::string::npos);
  REQUIRE(table.find("| [0.0, 0.1) | 0 | - | - | - | - | - | - | not checked |") != std::string::npos);
}

TEST_CASE("candidate_probability is 1 - (1 - J^r)^b", "[demo]") {
  // 1 - (1 - 0.5^4)^32 = 1 - (15/16)^32 = 0.873211
  REQUIRE(demo::candidate_probability(0.5, 4, 32) == Approx(0.873211).margin(1e-6));
  REQUIRE(demo::candidate_probability(0.0, 4, 32) == 0.0);
  REQUIRE(demo::candidate_probability(1.0, 4, 32) == 1.0);
}

TEST_CASE("is_candidate is true when the key lists are equal in one band", "[demo]") {
  REQUIRE(demo::is_candidate({1, 2, 3}, {9, 2, 8}));
  REQUIRE(!demo::is_candidate({1, 2, 3}, {3, 1, 2}));
  REQUIRE_THROWS_AS(demo::is_candidate({1, 2}, {1, 2, 3}), std::invalid_argument);
}

TEST_CASE("poisson_binomial_p_value is the exact two-sided tail", "[demo]") {
  // 4 fair coins: P(X = 0..4) = 1, 4, 6, 4, 1 out of 16.
  const std::vector<double> coins(4, 0.5);
  REQUIRE(demo::poisson_binomial_p_value(coins, 0) == Approx(2.0 / 16).margin(1e-12));
  REQUIRE(demo::poisson_binomial_p_value(coins, 1) == Approx(10.0 / 16).margin(1e-12));
  REQUIRE(demo::poisson_binomial_p_value(coins, 2) == 1.0);
  REQUIRE(demo::poisson_binomial_p_value(coins, 4) == Approx(2.0 / 16).margin(1e-12));
  // p = {0.2, 0.9}: P(X = 0) = 0.08, P(X = 1) = 0.74, P(X = 2) = 0.18.
  REQUIRE(demo::poisson_binomial_p_value({0.2, 0.9}, 2) == Approx(0.36).margin(1e-12));
  REQUIRE(demo::poisson_binomial_p_value({0.0, 1.0}, 1) == 1.0);
  REQUIRE(demo::poisson_binomial_p_value({0.0, 1.0}, 0) == 0.0);
  REQUIRE(demo::poisson_binomial_p_value({0.5}, 2) == 0.0);  // more than the pair count
}

TEST_CASE("scurve_counter bins the pairs and checks each bin", "[demo]") {
  // 1,000 pairs at J = 0.5, r = 4, b = 32: p = 0.873211, so expected = 873.21.
  const auto bin_with = [](std::size_t pairs, std::size_t candidates) {
    demo::scurve_counter counter(4, 32);
    for (std::size_t i = 0; i < pairs; ++i) {
      counter.add(0.5, i < candidates);
    }
    const auto bins = counter.bins();
    REQUIRE(bins.size() == demo::NUM_BINS);
    return bins[5];
  };
  const auto good = bin_with(1000, 873);
  REQUIRE(good.bin == 5);
  REQUIRE(good.pairs == 1000);
  REQUIRE(good.candidates == 873);
  REQUIRE(good.expected == Approx(873.211).margin(1e-3));
  const std::vector<double> probs(1000, demo::candidate_probability(0.5, 4, 32));
  REQUIRE(good.p_value == demo::poisson_binomial_p_value(probs, 873));
  REQUIRE(good.filled);
  REQUIRE(good.pass);

  const auto bad = bin_with(1000, 800);  // about 7 SD below the expected count
  REQUIRE(bad.filled);
  REQUIRE(bad.p_value < demo::SCURVE_ALPHA / demo::NUM_BINS);
  REQUIRE(!bad.pass);

  const auto small = bin_with(999, 873);  // fewer than MIN_BIN_PAIRS pairs
  REQUIRE(!small.filled);
  REQUIRE(!small.pass);
  REQUIRE(!demo::scurve_passes({good, small}));
  REQUIRE(demo::scurve_passes({good}));

  REQUIRE(demo::scurve_table({good}).find("| [0.5, 0.6) | 1000 | 873 | 873.21 | 0.873000 | 0.873211 | ") !=
          std::string::npos);
  REQUIRE(demo::scurve_table({bad}).find(" | FAIL |") != std::string::npos);
  REQUIRE(demo::scurve_table({small}).find("FAIL (bin not filled)") != std::string::npos);
}

namespace {

using datasketches::minhash_lsh_index;
using datasketches::minhash_sketch;

std::vector<uint64_t> real_keys(const minhash_sketch& s) {
  return minhash_lsh_index::band_keys(s, demo::LSH_BANDS);
}

// Band keys made from `rows_used` of the 4 slots of each band. With slot0_only, every slot is
// read as slot 0, as if every slot had the same seed.
std::vector<uint64_t> partial_keys(const minhash_sketch& s, unsigned rows_used, bool slot0_only) {
  const auto& sig = s.get_signature();
  std::vector<uint64_t> keys;
  for (uint64_t band = 0; band < demo::LSH_BANDS; ++band) {
    std::vector<uint64_t> slots;
    for (unsigned r = 0; r < rows_used; ++r) {
      slots.push_back(slot0_only ? sig[0] : sig[band * demo::LSH_ROWS + r]);
    }
    keys.push_back(XXHash64::hash(slots.data(), slots.size() * sizeof(uint64_t), s.get_seed() + band));
  }
  return keys;
}

}  // namespace

TEST_CASE("scurve_check passes the real band keys and fails broken ones", "[demo]") {
  const auto real = demo::scurve_check(real_keys, demo::LSH_ROWS, demo::LSH_BANDS);
  INFO(demo::scurve_table(real));
  for (const auto& b : real) {
    REQUIRE(b.pairs == demo::SCURVE_PAIRS_PER_BIN);
  }
  REQUIRE(demo::scurve_passes(real));

  // Each broken version must fail the check, as computed for 4 rows and 32 bands.
  const std::vector<std::pair<std::string, demo::band_key_function>> broken = {
      {"2-slot bands", [](const minhash_sketch& s) { return minhash_lsh_index::band_keys(s, 2 * demo::LSH_BANDS); }},
      // 4 is the smallest band loss that 2,000 pairs per bin detect. 1 missing band moves the rate by
      // about 1 point, which is too small; the test vectors of the library catch a wrong key count.
      {"4 missing bands", [](const minhash_sketch& s) {
         auto keys = real_keys(s);
         keys.resize(keys.size() - 4);
         return keys;
       }},
      {"a key from 3 of the 4 slots", [](const minhash_sketch& s) { return partial_keys(s, 3, false); }},
      {"the same seed in every slot", [](const minhash_sketch& s) { return partial_keys(s, 4, true); }},
  };
  for (const auto& variant : broken) {
    const auto bins = demo::scurve_check(variant.second, demo::LSH_ROWS, demo::LSH_BANDS);
    INFO(variant.first << "\n" << demo::scurve_table(bins));
    CHECK(!demo::scurve_passes(bins));
  }
}

TEST_CASE("pair_counts counts similar, verified and both", "[demo]") {
  demo::pair_counts counts;
  counts.add(true, true);
  counts.add(true, false);
  counts.add(false, true);
  REQUIRE(counts.similar == 2);
  REQUIRE(counts.verified == 2);
  REQUIRE(counts.both == 1);
}

TEST_CASE("largest_buckets orders by size, then band, then key", "[demo]") {
  // 2 bands. Item 2 is not indexed.
  // band 0: key 5 -> {0, 1, 3}; band 1: key 9 -> {0, 3}, key 4 -> {1}.
  const std::vector<std::vector<uint64_t>> keys{{5, 9}, {5, 4}, {}, {5, 9}};
  const auto buckets = demo::largest_buckets(keys, 10);
  REQUIRE(buckets.size() == 3);
  REQUIRE(buckets[0].band == 0);
  REQUIRE(buckets[0].key == 5);
  REQUIRE(buckets[0].items == std::vector<std::size_t>{0, 1, 3});
  REQUIRE(buckets[1].band == 1);
  REQUIRE(buckets[1].key == 9);
  REQUIRE(buckets[1].items == std::vector<std::size_t>{0, 3});
  REQUIRE(buckets[2].band == 1);
  REQUIRE(buckets[2].key == 4);
  REQUIRE(buckets[2].items == std::vector<std::size_t>{1});

  // equal sizes: band 0 before band 1, then the smaller key first
  const std::vector<std::vector<uint64_t>> ties{{8, 3}, {2, 6}};
  const auto tied = demo::largest_buckets(ties, 3);
  REQUIRE(tied.size() == 3);
  REQUIRE((tied[0].band == 0 && tied[0].key == 2));
  REQUIRE((tied[1].band == 0 && tied[1].key == 8));
  REQUIRE((tied[2].band == 1 && tied[2].key == 3));
}

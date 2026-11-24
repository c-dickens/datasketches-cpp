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
#include <sstream>
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

// MH-14b: the full-data check.

namespace {

// The joint probability as the ticket states it: a dynamic program over the bands, with
// Binomial(rows, J) equal slots in each band. State: (equal slots so far, some band all-equal).
// It is O(b k r) for each J, so the program uses the factorised form, which this checks.
double reported_by_bands(double j, unsigned rows, unsigned bands, std::size_t min_matches) {
  const std::size_t k = static_cast<std::size_t>(rows) * bands;
  std::vector<double> band(rows + 1);
  for (unsigned e = 0; e <= rows; ++e) {
    double choose = 1.0;
    for (unsigned x = 0; x < e; ++x) {
      choose = choose * (rows - x) / (x + 1);
    }
    band[e] = choose * std::pow(j, e) * std::pow(1.0 - j, rows - e);
  }
  std::vector<std::array<double, 2>> dist(k + 1, {0.0, 0.0});
  dist[0][0] = 1.0;
  for (unsigned b = 0; b < bands; ++b) {
    std::vector<std::array<double, 2>> next(k + 1, {0.0, 0.0});
    for (std::size_t s = 0; s + rows <= k; ++s) {
      for (unsigned e = 0; e <= rows; ++e) {
        next[s + e][e == rows ? 1 : 0] += dist[s][0] * band[e];
        next[s + e][1] += dist[s][1] * band[e];
      }
    }
    dist.swap(next);
  }
  double p = 0.0;
  for (std::size_t s = min_matches; s <= k; ++s) {
    p += dist[s][1];
  }
  return p;
}

}  // namespace

TEST_CASE("full_grid has every (k, r, t), in that order", "[demo]") {
  const auto grid = demo::full_grid();
  REQUIRE(grid.size() == 140);
  REQUIRE(grid.front().k == 64);
  REQUIRE(grid.front().rows == 2);
  REQUIRE(grid.front().bands == 32);
  REQUIRE(grid.front().threshold == 0.3);
  REQUIRE(grid[7].rows == 4);
  REQUIRE(grid.back().k == 1024);
  REQUIRE(grid.back().bands == 64);
  REQUIRE(grid.back().threshold == 0.9);
}

TEST_CASE("min_matches is the smallest m with m / k >= t", "[demo]") {
  REQUIRE(demo::min_matches(0.5, 64) == 32);
  REQUIRE(demo::min_matches(0.3, 64) == 20);  // 19.2 rounds up
  REQUIRE(demo::min_matches(0.9, 1024) == 922);
  REQUIRE(demo::min_matches(1.0, 4) == 4);
  REQUIRE_THROWS_AS(demo::min_matches(0.0, 4), std::invalid_argument);
}

TEST_CASE("binomial_pmf sums to 1 and handles J = 0 and J = 1", "[demo]") {
  const auto pmf = demo::binomial_pmf(0.3, 1024);
  double sum = 0.0;
  for (const double p : pmf) {
    sum += p;
  }
  REQUIRE(sum == Approx(1.0).margin(1e-12));
  REQUIRE(demo::binomial_pmf(0.5, 4)[2] == Approx(6.0 / 16).margin(1e-15));
  REQUIRE(demo::binomial_pmf(0.0, 4)[0] == 1.0);
  REQUIRE(demo::binomial_pmf(1.0, 4)[4] == 1.0);
}

TEST_CASE("full_band_given_matches counts the subsets with a full band", "[demo]") {
  // k = 4, bands {0, 1} and {2, 3}: of the 6 subsets of 2 slots, 2 fill a band. 3 slots always fill one.
  const auto q = demo::full_band_given_matches(2, 2);
  REQUIRE(q.size() == 5);
  REQUIRE(q[0] == 0.0);
  REQUIRE(q[1] == Approx(0.0).margin(1e-15));
  REQUIRE(q[2] == Approx(2.0 / 6).margin(1e-15));
  REQUIRE(q[3] == Approx(1.0).margin(1e-15));
  REQUIRE(q[4] == Approx(1.0).margin(1e-15));
}

TEST_CASE("reported_probability is the joint probability, not the product", "[demo]") {
  // b = 1, r = k: the band is all-equal only when all k slots are, so P = J^k when m <= k.
  REQUIRE(demo::reported_probability(0.7, 8, 1, demo::min_matches(0.5, 8)) == Approx(std::pow(0.7, 8)).margin(1e-14));
  // m = 0 (t = 0): P = P(candidate) = 1 - (1 - J^r)^b.
  for (const double j : {0.1, 0.5, 0.9}) {
    REQUIRE(demo::reported_probability(j, 4, 32, 0) == Approx(demo::candidate_probability(j, 4, 32)).margin(1e-12));
  }
  // k = 4, b = 2, r = 2, t = 0.75, so m = 3. By hand: 4 equal slots (J^4) fill both bands; each of the
  // 4 ways to have 3 equal slots leaves one band full (4 J^3 (1 - J)). At J = 0.5: 1/16 + 4/16 = 0.3125.
  // The product P(candidate) P(Ĵ >= t) = (2 J^2 - J^4)(5/16) = 0.13672 at J = 0.5, which is wrong.
  REQUIRE(demo::reported_probability(0.5, 2, 2, demo::min_matches(0.75, 4)) == Approx(0.3125).margin(1e-14));
  REQUIRE(demo::candidate_probability(0.5, 2, 2) * 0.3125 == Approx(0.13672).margin(1e-5));
  // The factorised form equals the dynamic program over the bands with Binomial(r, J) in each band.
  for (const unsigned rows : {2U, 3U, 4U}) {
    for (const double j : {0.05, 0.3, 0.5, 0.8, 0.99}) {
      for (std::size_t m = 0; m <= 12; ++m) {
        REQUIRE(demo::reported_probability(j, rows, 12 / rows, m) ==
                Approx(reported_by_bands(j, rows, 12 / rows, m)).margin(1e-13));
      }
    }
  }
}

TEST_CASE("check_pair_tests counts the pairs and tests both counts", "[demo]") {
  // t = 0.5: 2 true pairs, both reported; 2 pairs with 0 < J < 0.5, 1 reported.
  const std::vector<double> exact{0.9, 0.6, 0.4, 0.2};
  const std::vector<double> prob{0.99, 0.5, 0.3, 0.01};
  const std::vector<bool> reported{true, true, false, true};
  const auto c = demo::check_pair_tests(exact, prob, reported, 0, 0.5, 1e-6);
  REQUIRE(c.counts.similar == 2);
  REQUIRE(c.counts.verified == 3);
  REQUIRE(c.counts.both == 2);
  REQUIRE(c.false_pairs == 2);
  REQUIRE(c.expected_true == Approx(1.49).margin(1e-12));
  REQUIRE(c.expected_false == Approx(0.31).margin(1e-12));
  // P(X = 2) for {0.99, 0.5} is 0.495; P(X >= 1) for {0.3, 0.01} is 0.307.
  REQUIRE(c.p_recall == Approx(0.99).margin(1e-12));
  REQUIRE(c.p_precision == Approx(0.614).margin(1e-12));
  REQUIRE(c.pass);
  // One reported pair with J = 0 makes 2 reported false pairs: P(X >= 2) = 0.003, so p = 0.006.
  const auto d = demo::check_pair_tests(exact, prob, reported, 1, 0.5, 0.01);
  REQUIRE(d.counts.verified == 4);
  REQUIRE(d.p_precision == Approx(0.006).margin(1e-12));
  REQUIRE(!d.pass);
}

TEST_CASE("compare_clusters finds one merge and one split", "[demo]") {
  // True: {0, 1, 2}, {3, 4}, and 5 and 6 alone. LSH: {0, 1}, {3, 4, 5}, and 2 and 6 alone.
  // LSH splits {0, 1, 2}, and it merges {3, 4} with 5.
  const demo::edge_list true_edges{{0, 1}, {1, 2}, {3, 4}};
  const demo::edge_list lsh_edges{{0, 1}, {3, 4}, {4, 5}};
  const auto true_label = demo::components(7, true_edges);
  const auto lsh_label = demo::components(7, lsh_edges);
  REQUIRE(true_label == std::vector<std::size_t>{0, 0, 0, 3, 3, 5, 6});
  REQUIRE(demo::clusters_of(lsh_label) == std::vector<std::vector<std::size_t>>{{0, 1}, {3, 4, 5}});
  REQUIRE_THROWS_AS(demo::components(3, {{0, 3}}), std::invalid_argument);

  const auto m = demo::compare_clusters(lsh_label, true_label);
  REQUIRE(m.lsh_clusters == 2);
  REQUIRE(m.true_clusters == 2);
  REQUIRE(m.lsh_pairs == 4);   // {0, 1} and 3 pairs of {3, 4, 5}
  REQUIRE(m.true_pairs == 4);  // 3 pairs of {0, 1, 2} and {3, 4}
  REQUIRE(m.both_pairs == 2);  // {0, 1} and {3, 4}
  REQUIRE(m.merges == 1);
  REQUIRE(m.splits == 1);
  REQUIRE(m.largest_lsh == 3);
  REQUIRE(m.largest_true == 3);

  const auto worst = demo::worst_merges(lsh_label, true_label, lsh_edges, {0.9, 0.8, 0.35}, 5);
  REQUIRE(worst.size() == 1);
  REQUIRE(worst[0].lsh_root == 3);
  REQUIRE(worst[0].nodes == 3);
  REQUIRE(worst[0].true_parts == 2);
  REQUIRE(worst[0].weakest == std::pair<std::size_t, std::size_t>{4, 5});
  REQUIRE(worst[0].weakest_j == 0.35);
}

TEST_CASE("input_fingerprint changes when any set or id changes", "[demo]") {
  const std::vector<std::vector<std::string>> sets{{"a b", "c d"}, {"e f"}};
  const uint64_t base = demo::input_fingerprint({3, 7}, sets);
  REQUIRE(demo::input_fingerprint({3, 7}, sets) == base);
  REQUIRE(demo::input_fingerprint({3, 8}, sets) != base);
  REQUIRE(demo::input_fingerprint({3, 7}, {{"a b", "c e"}, {"e f"}}) != base);
  REQUIRE(demo::input_fingerprint({3, 7}, {{"a b"}, {"c d", "e f"}}) != base);  // the same items, other sets
  REQUIRE_THROWS_AS(demo::input_fingerprint({3}, sets), std::invalid_argument);
}

TEST_CASE("the exact-pair cache refuses a wrong header", "[demo]") {
  const std::string header = demo::exact_pairs_header("abc", 5, 3, 42);
  const std::vector<demo::exact_pair> pairs{{0, 1, 2, 5}, {1, 2, 1, 9}};
  std::stringstream good;
  demo::write_exact_pairs(good, header, pairs);
  std::vector<demo::exact_pair> read;
  REQUIRE(demo::read_exact_pairs(good, header, read));
  REQUIRE(read.size() == 2);
  REQUIRE(read[1].j == 2);
  REQUIRE(read[1].union_size == 9);

  for (const auto& other : {demo::exact_pairs_header("abd", 5, 3, 42), demo::exact_pairs_header("abc", 4, 3, 42),
                            demo::exact_pairs_header("abc", 5, 4, 42), demo::exact_pairs_header("abc", 5, 3, 43)}) {
    std::stringstream in;
    demo::write_exact_pairs(in, other, pairs);
    REQUIRE(!demo::read_exact_pairs(in, header, read));
    REQUIRE(read.empty());
  }
  std::stringstream bad(header + "# pairs=1\n0\t1\t2\n");
  REQUIRE_THROWS_AS(demo::read_exact_pairs(bad, header, read), std::runtime_error);
  // A cache that lost lines, or that has no pair count, is refused.
  std::stringstream truncated(header + "# pairs=2\n0\t1\t2\t5\n");
  REQUIRE_THROWS_AS(demo::read_exact_pairs(truncated, header, read), std::runtime_error);
  std::stringstream no_count(header + "0\t1\t2\t5\n");
  REQUIRE_THROWS_AS(demo::read_exact_pairs(no_count, header, read), std::runtime_error);
}

TEST_CASE("band_rates counts the pairs of each J band", "[demo]") {
  // Bands: 0.05 and 0.07 -> [0.0, 0.1); 0.55 -> [0.5, 0.6); 1.0 -> the last band; J = 0 is left out.
  const std::vector<double> exact{0.05, 0.07, 0.55, 1.0, 0.0};
  const std::vector<double> prob{0.01, 0.02, 0.6, 1.0, 0.0};
  const std::vector<bool> verified{false, true, true, true, true};
  const auto bands = demo::band_rates(exact, prob, verified);
  REQUIRE(bands[0].pairs == 2);
  REQUIRE(bands[0].verified == 1);
  REQUIRE(bands[0].expected == Approx(0.03).margin(1e-15));
  REQUIRE(bands[5].pairs == 1);
  REQUIRE(bands[5].verified == 1);
  REQUIRE(bands[9].pairs == 1);
  std::size_t total = 0;
  for (const auto& b : bands) {
    total += b.pairs;
  }
  REQUIRE(total == 4);
  REQUIRE_THROWS_AS(demo::band_rates(exact, prob, {true}), std::invalid_argument);
}

TEST_CASE("smallest_hashes chooses the keys with the smallest hash", "[demo]") {
  // For keys 0 .. n - 1 it chooses the same indices as sample_indices.
  std::vector<std::size_t> keys(100);
  for (std::size_t i = 0; i < keys.size(); ++i) {
    keys[i] = i;
  }
  auto chosen = demo::smallest_hashes(keys, 12, 1);
  REQUIRE(chosen.size() == 12);
  std::sort(chosen.begin(), chosen.end());
  REQUIRE(chosen == demo::sample_indices(100, 12, 1));
  // Positions, not keys, come back; fewer keys than count gives all of them.
  const auto few = demo::smallest_hashes({500, 7}, 12, 1);
  REQUIRE(few.size() == 2);
  REQUIRE(std::set<std::size_t>(few.begin(), few.end()) == std::set<std::size_t>{0, 1});
}

TEST_CASE("in_index_set splits by the parity of the hash", "[demo]") {
  std::size_t in_index = 0;
  for (std::size_t key = 0; key < 1000; ++key) {
    const auto value = static_cast<uint64_t>(key);
    REQUIRE(demo::in_index_set(key, 2) == (XXHash64::hash(&value, 8, 2) % 2 == 0));
    in_index += demo::in_index_set(key, 2) ? 1 : 0;
  }
  // About half; the bound is 5 SD of Binomial(1000, 1/2), which is 79.
  REQUIRE(in_index > 500 - 79);
  REQUIRE(in_index < 500 + 79);
}

TEST_CASE("bucket_table gives the items that share a band key with the query", "[demo]") {
  // Items 10, 20, 30 with 3 bands. Item 20 shares band 1 with the query; item 30 shares bands 0 and 2.
  const demo::bucket_table table({{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}, {10, 20, 30});
  REQUIRE(table.candidates({7, 5, 9}) == std::vector<std::size_t>{20, 30});
  REQUIRE(table.candidates({0, 0, 0}).empty());
  REQUIRE(table.candidates({1, 2, 3}) == std::vector<std::size_t>{10});
  REQUIRE_THROWS_AS(demo::bucket_table({{1}}, {1, 2}), std::invalid_argument);
}

TEST_CASE("bucket_table equals query_candidates of the index", "[demo]") {
  std::vector<datasketches::minhash_sketch> sketches;
  std::vector<std::vector<uint64_t>> keys;
  std::vector<std::size_t> ids;
  datasketches::minhash_lsh_index index(8, 0.5);
  for (uint64_t i = 0; i < 50; ++i) {
    auto s = datasketches::minhash_sketch::builder().set_k(32).build();
    for (uint64_t x = (i % 5) * 7; x < (i % 5) * 7 + 40; ++x) {  // 5 groups of overlapping sets
      s.update(x * 1000 + (x % 3 == 0 ? i : 0));
    }
    const auto k = datasketches::minhash_lsh_index::band_keys(s, 8);
    if (i % 2 == 0) {
      index.add(i, s);
      keys.emplace_back(k.begin(), k.end());
      ids.push_back(i);
    }
    sketches.push_back(s);
  }
  const demo::bucket_table table(keys, ids);
  for (uint64_t q = 1; q < 50; q += 2) {
    const auto expected = index.query_candidates(sketches[q]);
    const auto k = datasketches::minhash_lsh_index::band_keys(sketches[q], 8);
    REQUIRE(table.candidates(std::vector<uint64_t>(k.begin(), k.end())) ==
            std::vector<std::size_t>(expected.begin(), expected.end()));
  }
}

TEST_CASE("compare_kept counts the kept, wrongly removed and missed items", "[demo]") {
  // True: {0, 1, 2}, {3, 4}, 5 and 6 alone, so 0, 3, 5 and 6 are kept.
  // LSH: {0, 1}, {3, 4, 5}, 2 and 6 alone, so 0, 2, 3 and 6 are kept.
  const auto k = demo::compare_kept({0, 0, 2, 3, 3, 3, 6}, {0, 0, 0, 3, 3, 5, 6});
  REQUIRE(k.items == 7);
  REQUIRE(k.kept_exact == 4);
  REQUIRE(k.kept_lsh == 4);
  REQUIRE(k.kept_both == 3);
  REQUIRE(k.wrongly_removed == 1);    // 5: LSH merged it into {3, 4}
  REQUIRE(k.missed_duplicates == 1);  // 2: LSH split it from {0, 1}
  REQUIRE_THROWS_AS(demo::compare_kept({0}, {0, 1}), std::invalid_argument);
}

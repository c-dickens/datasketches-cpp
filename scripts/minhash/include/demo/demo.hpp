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

#ifndef DEMO_DEMO_HPP_
#define DEMO_DEMO_HPP_

// Fork-only demo code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Shared helpers of the MinHash and LSH demos: the pairs to check, and the
// per-bin statistics that compare Jaccard estimates with exact values.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "minhash_sketch.hpp"

namespace demo {

constexpr std::array<uint16_t, 4> K_VALUES = {64, 128, 256, 512};
constexpr std::size_t NUM_BINS = 10;          // J bins of width 0.1
constexpr std::size_t MIN_BIN_PAIRS = 1000;   // a bin with fewer pairs is reported, not checked
constexpr double MAX_MEAN_BIAS = 0.01;
constexpr double SPREAD_TOLERANCE = 0.15;
constexpr double NOMINAL_COVERAGE_2SD = 0.9545;
constexpr double NOMINAL_COVERAGE_3SD = 0.9973;
constexpr double COVERAGE_SLACK = 0.01;       // "minus 1 point"
constexpr uint64_t SAMPLE_SEED = 1;
constexpr std::size_t SAMPLE_SIZE = 2000;
constexpr std::size_t MERGE_PIECES = 4;

/// The bin of an exact J: floor(10 J). J = 1 is in bin 9.
std::size_t jaccard_bin(double j);

/// The `count` indices i in [0, n) with the smallest XXHash64::hash(&i, 8, seed),
/// with i as uint64_t and ties by i. The result is in ascending order.
/// Throws std::invalid_argument if count > n.
std::vector<std::size_t> sample_indices(std::size_t n, std::size_t count, uint64_t seed);

/// The pairs to check: all pairs of sample_indices(keys.size(), sample_size, seed), plus all
/// pairs i < j with keys[i] == keys[j] and keys[i] not empty. Each pair is (smaller, larger).
/// The result is sorted and has no duplicates.
std::vector<std::pair<std::size_t, std::size_t>> check_pairs(
    const std::vector<std::string>& keys, std::size_t sample_size, uint64_t seed);

/// A sketch with k slots and the default seed (9001) that received every item.
datasketches::minhash_sketch sketch_of(const std::vector<std::string>& items, uint16_t k);

struct pair_estimate {
  double exact;                   // exact J
  double estimate;                // a.jaccard(b)
  std::array<double, 3> bounds2;  // a.jaccard_bounds(b, 2)
  std::array<double, 3> bounds3;  // a.jaccard_bounds(b, 3)
};

struct bin_summary {
  std::size_t bin;
  std::size_t pairs;
  double mean_bias;
  double observed_sd;
  double expected_sd;
  double coverage2;
  double coverage3;
  bool checked;  // pairs >= MIN_BIN_PAIRS
  bool bias_pass;
  bool spread_pass;
  bool coverage2_pass;
  bool coverage3_pass;
};

/// Puts the pairs of one k into NUM_BINS bins. Pairs with exact J = 0 or J = 1 are left out.
/// The result has NUM_BINS entries, in bin order, also for a bin with no pair.
std::vector<bin_summary> summarise_bins(const std::vector<pair_estimate>& pairs, uint16_t k);

/// True if the bin is not checked, or if it passes every criterion.
bool bin_passes(const bin_summary& bin);

/// A Markdown table with one row for each bin: the pair count, each figure, and PASS, FAIL
/// or "not checked" for each criterion.
std::string bins_table(const std::vector<bin_summary>& bins);

// LSH demo (MH-14). MH-15 uses these names too.

constexpr uint16_t LSH_K = 128;
constexpr uint16_t LSH_BANDS = 32;
constexpr unsigned LSH_ROWS = 4;           // LSH_K / LSH_BANDS
constexpr double LSH_THRESHOLD = 0.5;
constexpr std::size_t LARGEST_BUCKETS = 10;
constexpr std::size_t BUCKET_EXAMPLES = 3;

/// 1 - (1 - j^rows)^bands.
double candidate_probability(double j, unsigned rows, unsigned bands);

/// True when the two key lists are equal in at least one position (band).
/// Throws std::invalid_argument if the sizes differ.
bool is_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b);

constexpr double SCURVE_ALPHA = 1e-6;              // false-FAIL rate of the whole table (Bonferroni over the bins)
constexpr std::size_t SCURVE_PAIRS_PER_BIN = 2000;
constexpr uint64_t SCURVE_UNION = 1000;            // |S1 ∪ S2| of each synthetic pair

/// The two-sided exact p-value of `observed` for a sum of independent Bernoulli(p_i):
/// min(1, 2 min(P(X <= observed), P(X >= observed))).
double poisson_binomial_p_value(const std::vector<double>& probs, std::size_t observed);

struct scurve_bin {
  std::size_t bin;
  std::size_t pairs;
  std::size_t candidates;
  double expected;  // sum of p_i, with p_i = candidate_probability(J_i, rows, bands)
  double p_value;   // poisson_binomial_p_value of the candidate count
  bool filled;      // pairs >= MIN_BIN_PAIRS
  bool pass;        // filled and p_value >= SCURVE_ALPHA / NUM_BINS
};

/// Collects the S-curve check one pair at a time. It keeps each p_i, for the exact p-value.
class scurve_counter {
public:
  scurve_counter(unsigned rows, unsigned bands);
  void add(double exact, bool candidate);
  std::vector<scurve_bin> bins() const;  // NUM_BINS bins, by jaccard_bin(exact)

private:
  unsigned rows_;
  unsigned bands_;
  std::array<std::vector<double>, NUM_BINS> probs_{};
  std::array<std::size_t, NUM_BINS> candidates_{};
};

/// True when every bin passes.
bool scurve_passes(const std::vector<scurve_bin>& bins);

/// The band keys of one sketch. The real function is minhash_lsh_index::band_keys(sketch, LSH_BANDS).
using band_key_function = std::function<std::vector<uint64_t>(const datasketches::minhash_sketch&)>;

/// The S-curve check on synthetic integer sets. Each bin gets SCURVE_PAIRS_PER_BIN pairs
/// S1 = [o, o + a + c) and S2 = [o + a, o + 2a + c), with 2a + c = SCURVE_UNION, so J = c / SCURVE_UNION.
/// c takes the even values of the bin in turn. Pair i has o = i * 2^32, so no item is in 2 pairs.
/// The sketches have k = rows * bands and the default seed. A pair is a candidate when
/// is_candidate(keys_of(s1), keys_of(s2)).
std::vector<scurve_bin> scurve_check(const band_key_function& keys_of, unsigned rows, unsigned bands);

/// A Markdown table with one row for each bin, and PASS, FAIL or "FAIL (bin not filled)".
std::string scurve_table(const std::vector<scurve_bin>& bins);

/// Counts for recall and precision. recall = both / similar, precision = both / verified.
struct pair_counts {
  std::size_t similar = 0;   // exact J >= LSH_THRESHOLD
  std::size_t verified = 0;  // candidate and estimated J >= LSH_THRESHOLD
  std::size_t both = 0;
  void add(bool is_similar, bool is_verified);
};

/// One bucket: the items that share one band key in one band.
struct bucket {
  uint16_t band;
  uint64_t key;
  std::vector<std::size_t> items;  // ascending
};

/// The `count` largest buckets: by size (descending), then band, then key.
/// keys_per_item[i] is band_keys of item i. An empty list means that item i is not indexed.
std::vector<bucket> largest_buckets(const std::vector<std::vector<uint64_t>>& keys_per_item, std::size_t count);

}  // namespace demo

#endif  // DEMO_DEMO_HPP_

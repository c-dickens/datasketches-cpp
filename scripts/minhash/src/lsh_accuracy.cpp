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

// Profile lsh_accuracy: the LSH candidate rate against the theory 1 - (1 - J^r)^b, and the
// reported rate (candidate and estimated J >= t). It uses band_keys() and the rule "a candidate
// when the keys are equal in at least one band", not the index, so one signature gives every b.
// B = [0, i) and A = [0, u), so J = i / u exactly. For J = 0, B is a disjoint set of 100 items.
// Trial t uses the seed DEFAULT_SEED + t.

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "characterise/common.hpp"
#include "characterise/profiles.hpp"
#include "common_defs.hpp"
#include "minhash_lsh.hpp"
#include "minhash_sketch.hpp"

namespace characterise {

namespace {

constexpr std::array<uint16_t, 3> LSH_K_VALUES = {64, 128, 256};
constexpr std::array<uint16_t, 2> SMOKE_LSH_K_VALUES = {64, 128};
constexpr std::array<double, 3> THRESHOLDS = {0.5, 0.7, 0.9};
constexpr uint64_t UNION_SIZE = 10000;
constexpr uint64_t FULL_TRIALS = 4096;
constexpr uint64_t SMOKE_TRIALS = 256;
constexpr uint64_t FULL_J_STEPS = 100;  // J = j / 100, j = 0 ... 100
constexpr uint64_t SMOKE_J_STEPS = 20;  // J = j / 20, j = 0 ... 20
constexpr uint64_t DISJOINT_START = static_cast<uint64_t>(1) << 40;  // the J = 0 set: [2^40, 2^40 + 100)
constexpr uint64_t DISJOINT_SIZE = 100;

using sketch = datasketches::minhash_sketch;
using index = datasketches::minhash_lsh_index;

// The counts of one (k, b, J) point over all trials.
struct counts {
  uint64_t candidate;
  std::array<uint64_t, THRESHOLDS.size()> reported;
};

void write_row(std::ostream& out, uint16_t k, uint16_t b, double j, uint64_t trials, const counts& c) {
  const auto t = static_cast<double>(trials);
  const auto r = static_cast<unsigned>(k / b);
  const double rate = static_cast<double>(c.candidate) / t;
  const double theory = lsh_theory_rate(j, r, b);
  out << k << "\t" << b << "\t" << r << "\t" << j << "\t" << trials << "\t" << rate << "\t" << theory << "\t";
  if (theory == 0.0 || theory == 1.0) {
    out << (rate == theory ? "0" : "NA");
  } else {
    out << (rate - theory) / std::sqrt(theory * (1.0 - theory) / t);
  }
  for (const uint64_t n : c.reported) {
    out << "\t" << static_cast<double>(n) / t;
  }
  out << "\n";
}

}  // namespace

void lsh_accuracy(std::ostream& out, bool smoke) {
  const std::vector<uint16_t> k_values =
      smoke ? std::vector<uint16_t>(SMOKE_LSH_K_VALUES.begin(), SMOKE_LSH_K_VALUES.end())
            : std::vector<uint16_t>(LSH_K_VALUES.begin(), LSH_K_VALUES.end());
  const uint64_t trials = smoke ? SMOKE_TRIALS : FULL_TRIALS;
  const uint64_t j_steps = smoke ? SMOKE_J_STEPS : FULL_J_STEPS;

  out << std::setprecision(10);
  write_metadata(out, smoke ? "minhash-lsh-accuracy-smoke" : "minhash-lsh-accuracy");
  out << "# trials=" << trials << "\n";
  out << "# union_size=" << UNION_SIZE << "\n";
  out << "# seed_base=" << datasketches::DEFAULT_SEED << "\n";
  out << "# k_values=";
  for (std::size_t i = 0; i < k_values.size(); ++i) {
    out << (i == 0 ? "" : ",") << k_values[i];
  }
  out << "\n";
  out << "# thresholds=";
  for (std::size_t i = 0; i < THRESHOLDS.size(); ++i) {
    out << (i == 0 ? "" : ",") << THRESHOLDS[i];
  }
  out << "\n";
  out << "K\tB\tR\tTrueJ\tTrials\tCandidateRate\tTheoryRate\tCandidateZ";
  for (const double t : THRESHOLDS) {
    out << "\tReportedRate_t" << t;
  }
  out << "\n";

  // sizes[p] = |B| at grid point p; p = 0 is the disjoint set, and J = sizes[p] / u for p > 0.
  std::vector<uint64_t> sizes;
  for (uint64_t step = 0; step <= j_steps; ++step) {
    sizes.push_back(step * UNION_SIZE / j_steps);
  }

  const auto start = std::chrono::steady_clock::now();
  for (const uint16_t k : k_values) {
    std::vector<uint16_t> b_values;
    for (uint16_t b = 1; b <= k; b = static_cast<uint16_t>(b * 2)) {
      b_values.push_back(b);
    }
    std::vector<std::vector<counts>> table(b_values.size(), std::vector<counts>(sizes.size(), counts{0, {}}));

    std::vector<sketch> bs;  // bs[p] is the B sketch of grid point p
    std::vector<double> estimates(sizes.size());
    bs.reserve(sizes.size());
    for (uint64_t t = 0; t < trials; ++t) {
      const uint64_t seed = datasketches::DEFAULT_SEED + t;
      auto a = sketch::builder().set_k(k).set_seed(seed).build();
      bs.clear();
      auto disjoint = sketch::builder().set_k(k).set_seed(seed).build();
      for (uint64_t item = DISJOINT_START; item < DISJOINT_START + DISJOINT_SIZE; ++item) {
        disjoint.update(item);
      }
      bs.push_back(disjoint);
      for (uint64_t item = 0; item < UNION_SIZE; ++item) {
        a.update(item);
        if (bs.size() < sizes.size() && item + 1 == sizes[bs.size()]) {
          bs.push_back(a);
        }
      }
      for (std::size_t p = 0; p < sizes.size(); ++p) {
        estimates[p] = a.jaccard(bs[p]);
      }
      for (std::size_t bi = 0; bi < b_values.size(); ++bi) {
        const auto keys_a = index::band_keys(a, b_values[bi]);
        for (std::size_t p = 0; p < sizes.size(); ++p) {
          if (!lsh_candidate(keys_a, index::band_keys(bs[p], b_values[bi]))) {
            continue;
          }
          auto& c = table[bi][p];
          ++c.candidate;
          for (std::size_t ti = 0; ti < THRESHOLDS.size(); ++ti) {
            if (estimates[p] >= THRESHOLDS[ti]) {
              ++c.reported[ti];
            }
          }
        }
      }
    }

    for (std::size_t bi = 0; bi < b_values.size(); ++bi) {
      for (std::size_t p = 0; p < sizes.size(); ++p) {
        const double j = p == 0 ? 0.0 : static_cast<double>(sizes[p]) / UNION_SIZE;
        write_row(out, k, b_values[bi], j, trials, table[bi][p]);
      }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cerr << "lsh_accuracy: k = " << k << " done, " << std::fixed << std::setprecision(1) << elapsed.count()
              << " s elapsed\n";
  }
}

}  // namespace characterise

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

// Profile accuracy_j: the error of the MinHash Jaccard estimate against the true J.
// B = [0, i) and A = [0, u), so J = i / u exactly. Trial t uses the seed DEFAULT_SEED + t.

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
#include "minhash_sketch.hpp"

namespace characterise {

namespace {

constexpr uint64_t UNION_SIZE = 10000;
constexpr uint64_t FULL_TRIALS = 4096;
constexpr uint64_t SMOKE_TRIALS = 256;
constexpr uint64_t FULL_J_STEPS = 100;   // J = j / 100, j = 1 ... 99
constexpr uint64_t SMOKE_J_STEPS = 20;   // J = j / 20, j = 1 ... 19
constexpr uint8_t MAX_SD_LEVEL = 3;      // coverage at 1, 2 and 3 SD

using sketch = datasketches::minhash_sketch;

// The results of one (k, J) point over all trials.
struct point {
  uint64_t size;  // i = |B|
  double j;       // i / u
  datasketches::kll_sketch<double> abs_err;
  datasketches::kll_sketch<double> norm_err;
  double sum_err;
  double sum_sq_err;
  std::array<uint64_t, MAX_SD_LEVEL> covered;
};

void write_row(std::ostream& out, uint16_t k, uint64_t trials, const point& p) {
  const auto t = static_cast<double>(trials);
  const double mean_bias = p.sum_err / t;
  const double theory_sd = std::sqrt(p.j * (1.0 - p.j) / k);
  // sum (e - mean)^2 = sum e^2 - T mean^2
  const double measured_sd = std::sqrt((p.sum_sq_err - t * mean_bias * mean_bias) / (t - 1.0));
  out << k << "\t" << p.j << "\t" << trials << "\t" << p.j + mean_bias << "\t" << mean_bias << "\t"
      << mean_bias / (theory_sd / std::sqrt(t)) << "\t" << theory_sd << "\t" << measured_sd << "\t"
      << measured_sd / theory_sd << "\t";
  write_quantiles(out, p.abs_err);
  out << "\t";
  write_quantiles(out, p.norm_err);
  for (const uint64_t c : p.covered) {
    out << "\t" << static_cast<double>(c) / t;
  }
  out << "\n";
}

}  // namespace

void accuracy_j(std::ostream& out, bool smoke) {
  const std::vector<uint16_t> k_values = smoke ? std::vector<uint16_t>(SMOKE_K_VALUES.begin(), SMOKE_K_VALUES.end())
                                               : std::vector<uint16_t>(K_VALUES.begin(), K_VALUES.end());
  const uint64_t trials = smoke ? SMOKE_TRIALS : FULL_TRIALS;
  const uint64_t j_steps = smoke ? SMOKE_J_STEPS : FULL_J_STEPS;

  out << std::setprecision(10);
  write_metadata(out, smoke ? "minhash-accuracy-j-smoke" : "minhash-accuracy-j");
  out << "# trials=" << trials << "\n";
  out << "# union_size=" << UNION_SIZE << "\n";
  out << "# seed_base=" << datasketches::DEFAULT_SEED << "\n";
  out << "# k_values=";
  for (std::size_t i = 0; i < k_values.size(); ++i) {
    out << (i == 0 ? "" : ",") << k_values[i];
  }
  out << "\n";
  out << "K\tTrueJ\tTrials\tMeanEst\tMeanBias\tBiasOverSE\tTheorySD\tMeasuredSD\tSDRatio\t"
      << quantile_header("AbsErr") << "\t" << quantile_header("NormErr") << "\tCover1SD\tCover2SD\tCover3SD\n";

  const auto start = std::chrono::steady_clock::now();
  for (const uint16_t k : k_values) {
    std::vector<point> points;
    for (uint64_t step = 1; step < j_steps; ++step) {
      const uint64_t size = step * UNION_SIZE / j_steps;
      points.push_back(point{size, static_cast<double>(size) / UNION_SIZE, datasketches::kll_sketch<double>(KLL_K),
                             datasketches::kll_sketch<double>(KLL_K), 0.0, 0.0, {}});
    }

    std::vector<sketch> prefixes;  // prefixes[p] is the sketch of B = [0, points[p].size)
    prefixes.reserve(points.size());
    for (uint64_t t = 0; t < trials; ++t) {
      auto a = sketch::builder().set_k(k).set_seed(datasketches::DEFAULT_SEED + t).build();
      prefixes.clear();
      for (uint64_t item = 0; item < UNION_SIZE; ++item) {
        a.update(item);
        if (prefixes.size() < points.size() && item + 1 == points[prefixes.size()].size) {
          prefixes.push_back(a);
        }
      }
      for (std::size_t p = 0; p < points.size(); ++p) {
        auto& pt = points[p];
        const double est = a.jaccard(prefixes[p]);
        const double err = est - pt.j;
        pt.abs_err.update(err);
        pt.norm_err.update(normalised_error(est, pt.j, k));
        pt.sum_err += err;
        pt.sum_sq_err += err * err;
        for (uint8_t s = 1; s <= MAX_SD_LEVEL; ++s) {
          if (covers(a.jaccard_bounds(prefixes[p], s), pt.j)) {
            ++pt.covered[s - 1];
          }
        }
      }
    }

    for (const auto& pt : points) {
      write_row(out, k, trials, pt);
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cerr << "accuracy_j: k = " << k << " done, " << std::fixed << std::setprecision(1) << elapsed.count()
              << " s elapsed\n";
  }
}

}  // namespace characterise

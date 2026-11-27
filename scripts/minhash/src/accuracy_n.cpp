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

// Profile accuracy_n: the error of the MinHash and theta Jaccard estimates against the union
// size n, at J = 0.1, 0.5 and 0.9. A = [0, n), and B_m holds the items x of A with in_b(x, m),
// so the true J at each n is exact, and one pass gives every n and every m.
// Trial t uses the seed DEFAULT_SEED + t for both sketches.
// Profile accuracy_n_half is the same, with theta at half the nominal k: theta then never uses
// more memory than MinHash.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "characterise/common.hpp"
#include "characterise/profiles.hpp"
#include "common_defs.hpp"
#include "minhash_sketch.hpp"
#include "theta_jaccard_similarity.hpp"
#include "theta_sketch.hpp"

namespace characterise {

namespace {

constexpr uint64_t FULL_TRIALS = 4096;
constexpr uint64_t SMOKE_TRIALS = 256;
constexpr uint64_t N_START = 2;
constexpr uint64_t FULL_N_END = 65536;
constexpr uint64_t SMOKE_N_END = 4096;
constexpr unsigned FULL_PPO = 16;
constexpr unsigned SMOKE_PPO = 4;
constexpr std::array<unsigned, 3> M_VALUES = {1, 5, 9};  // J = m / 10
constexpr uint8_t MAX_SD_LEVEL = 3;                       // MinHash coverage at 1, 2 and 3 SD
// accuracy_n_half: no k = 16, because theta at lg_k = 3 is below its MIN_LG_K of 4.
constexpr std::array<uint16_t, 4> HALF_K_VALUES = {64, 256, 1024, 4096};
constexpr std::array<uint16_t, 2> SMOKE_HALF_K_VALUES = {64, 256};

using sketch = datasketches::minhash_sketch;
using theta = datasketches::update_theta_sketch;

// The error of one sketch at one (k, m, n) point over all trials.
struct error_stats {
  datasketches::kll_sketch<double> abs_err;
  double sum_err;
  double sum_sq_err;

  void update(double err) {
    abs_err.update(err);
    sum_err += err;
    sum_sq_err += err * err;
  }
};

// The results of one (k, m, n) point over all trials.
struct point {
  uint64_t n;
  double j;  // ((n m) / 10) / n
  error_stats mh;
  error_stats th;
  std::array<uint64_t, MAX_SD_LEVEL> mh_covered;
  uint64_t th_covered;
  uint64_t retained_min;
  uint64_t retained_max;
  uint64_t bytes_min;
  uint64_t bytes_max;
  uint64_t ser_bytes_min;
  uint64_t ser_bytes_max;
  uint64_t mh_ser_bytes;
};

void write_bias_sd(std::ostream& out, uint64_t trials, const error_stats& e) {
  const auto t = static_cast<double>(trials);
  const double mean_bias = e.sum_err / t;
  // sum (e - mean)^2 = sum e^2 - T mean^2. It can round below 0 when every error is equal.
  const double measured_sd = std::sqrt(std::max(0.0, (e.sum_sq_err - t * mean_bias * mean_bias) / (t - 1.0)));
  out << mean_bias << "\t" << measured_sd << "\t";
  write_quantiles(out, e.abs_err);
}

void write_row(std::ostream& out, uint16_t k, unsigned m, uint64_t trials, const point& p) {
  const auto t = static_cast<double>(trials);
  out << k << "\t" << m / 10.0 << "\t" << p.n << "\t" << p.j << "\t" << trials << "\t"
      << std::sqrt(p.j * (1.0 - p.j) / k) << "\t";
  write_bias_sd(out, trials, p.mh);
  for (const uint64_t c : p.mh_covered) {
    out << "\t" << static_cast<double>(c) / t;
  }
  out << "\t";
  write_bias_sd(out, trials, p.th);
  out << "\t" << static_cast<double>(p.th_covered) / t << "\t" << p.retained_min << "\t" << p.retained_max << "\t"
      << p.bytes_min << "\t" << p.bytes_max << "\t" << 8 * static_cast<uint64_t>(k) << "\t" << p.ser_bytes_min
      << "\t" << p.ser_bytes_max << "\t" << p.mh_ser_bytes << "\n";
}

// Runs the profile `name`. MinHash uses k, and theta uses lg_k = log2(k) - theta_lg_k_offset.
void run(std::ostream& out, const std::string& name, const std::vector<uint16_t>& k_values, uint8_t theta_lg_k_offset,
         bool smoke) {
  const uint64_t trials = smoke ? SMOKE_TRIALS : FULL_TRIALS;
  const unsigned ppo = smoke ? SMOKE_PPO : FULL_PPO;
  const std::vector<uint64_t> n_points = pwr_2_law_points(N_START, smoke ? SMOKE_N_END : FULL_N_END, ppo);

  out << std::setprecision(10);
  write_metadata(out, name);
  out << "# trials=" << trials << "\n";
  out << "# seed_base=" << datasketches::DEFAULT_SEED << "\n";
  out << "# k_values=";
  for (std::size_t i = 0; i < k_values.size(); ++i) {
    out << (i == 0 ? "" : ",") << k_values[i];
  }
  out << "\n";
  out << "# ppo=" << ppo << "\n";
  out << "# theta_bounds=2SD\n";
  if (theta_lg_k_offset != 0) {  // no line at offset 0, so the accuracy_n output stays as committed
    out << "# theta_lg_k=log2(K)-" << static_cast<unsigned>(theta_lg_k_offset) << "\n";
  }
  out << "K\tTargetJ\tN\tTrueJ\tTrials\tTheorySD\tMH_MeanBias\tMH_MeasuredSD\t" << quantile_header("MH_AbsErr")
      << "\tMH_Cover1SD\tMH_Cover2SD\tMH_Cover3SD\tTH_MeanBias\tTH_MeasuredSD\t" << quantile_header("TH_AbsErr")
      << "\tTH_Cover2SD\tTH_RetainedA_Min\tTH_RetainedA_Max\tTH_BytesA_Min\tTH_BytesA_Max\tMH_Bytes"
      << "\tTH_SerBytesA_Min\tTH_SerBytesA_Max\tMH_SerBytes\n";

  const auto start = std::chrono::steady_clock::now();
  for (const uint16_t k : k_values) {
    const auto lg_k = static_cast<uint8_t>(std::lround(std::log2(k)) - theta_lg_k_offset);  // theta only

    // points[mi][p] is the point of M_VALUES[mi] at n_points[p], or has n = 0 when J would be 0 or 1.
    std::vector<std::vector<point>> points(M_VALUES.size());
    for (std::size_t mi = 0; mi < M_VALUES.size(); ++mi) {
      for (const uint64_t n : n_points) {
        const uint64_t b = n * M_VALUES[mi] / 10;
        const bool row = b > 0 && b < n;
        points[mi].push_back(point{row ? n : 0, static_cast<double>(b) / static_cast<double>(n),
                                   error_stats{datasketches::kll_sketch<double>(KLL_K), 0.0, 0.0},
                                   error_stats{datasketches::kll_sketch<double>(KLL_K), 0.0, 0.0},
                                   {},
                                   0,
                                   std::numeric_limits<uint64_t>::max(),
                                   0,
                                   std::numeric_limits<uint64_t>::max(),
                                   0,
                                   std::numeric_limits<uint64_t>::max(),
                                   0,
                                   0});
      }
    }

    for (uint64_t t = 0; t < trials; ++t) {
      const uint64_t seed = datasketches::DEFAULT_SEED + t;
      auto a = sketch::builder().set_k(k).set_seed(seed).build();
      auto ta = theta::builder().set_lg_k(lg_k).set_seed(seed).build();
      std::vector<sketch> b;
      std::vector<theta> tb;
      for (std::size_t mi = 0; mi < M_VALUES.size(); ++mi) {
        b.push_back(sketch::builder().set_k(k).set_seed(seed).build());
        tb.push_back(theta::builder().set_lg_k(lg_k).set_seed(seed).build());
      }

      std::size_t next = 0;  // the index of the next n point
      for (uint64_t x = 0; next < n_points.size(); ++x) {
        a.update(x);
        ta.update(x);
        for (std::size_t mi = 0; mi < M_VALUES.size(); ++mi) {
          if (in_b(x, M_VALUES[mi])) {
            b[mi].update(x);
            tb[mi].update(x);
          }
        }
        if (x + 1 != n_points[next]) {
          continue;
        }
        const auto retained = static_cast<uint64_t>(ta.get_num_retained());
        const uint64_t bytes = theta_compact_bytes(ta);
        const auto ser_bytes = static_cast<uint64_t>(ta.compact().get_serialized_size_bytes());
        const auto mh_ser_bytes = static_cast<uint64_t>(a.get_serialized_size_bytes());
        for (std::size_t mi = 0; mi < M_VALUES.size(); ++mi) {
          auto& pt = points[mi][next];
          if (pt.n == 0) {
            continue;
          }
          pt.mh.update(a.jaccard(b[mi]) - pt.j);
          for (uint8_t s = 1; s <= MAX_SD_LEVEL; ++s) {
            if (covers(a.jaccard_bounds(b[mi], s), pt.j)) {
              ++pt.mh_covered[s - 1];
            }
          }
          const auto r = datasketches::theta_jaccard_similarity::jaccard(ta, tb[mi], seed);
          pt.th.update(r[1] - pt.j);
          if (covers(r, pt.j)) {
            ++pt.th_covered;
          }
          pt.retained_min = std::min(pt.retained_min, retained);
          pt.retained_max = std::max(pt.retained_max, retained);
          pt.bytes_min = std::min(pt.bytes_min, bytes);
          pt.bytes_max = std::max(pt.bytes_max, bytes);
          pt.ser_bytes_min = std::min(pt.ser_bytes_min, ser_bytes);
          pt.ser_bytes_max = std::max(pt.ser_bytes_max, ser_bytes);
          pt.mh_ser_bytes = mh_ser_bytes;
        }
        ++next;
      }
    }

    for (std::size_t mi = 0; mi < M_VALUES.size(); ++mi) {
      for (const auto& pt : points[mi]) {
        if (pt.n != 0) {
          write_row(out, k, M_VALUES[mi], trials, pt);
        }
      }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cerr << name << ": k = " << k << " done, " << std::fixed << std::setprecision(1) << elapsed.count()
              << " s elapsed\n";
  }
}

}  // namespace

void accuracy_n(std::ostream& out, bool smoke) {
  run(out, smoke ? "minhash-accuracy-n-smoke" : "minhash-accuracy-n",
      smoke ? std::vector<uint16_t>(SMOKE_K_VALUES.begin(), SMOKE_K_VALUES.end())
            : std::vector<uint16_t>(K_VALUES.begin(), K_VALUES.end()),
      0, smoke);
}

void accuracy_n_half(std::ostream& out, bool smoke) {
  run(out, smoke ? "minhash-accuracy-n-half-smoke" : "minhash-accuracy-n-half",
      smoke ? std::vector<uint16_t>(SMOKE_HALF_K_VALUES.begin(), SMOKE_HALF_K_VALUES.end())
            : std::vector<uint16_t>(HALF_K_VALUES.begin(), HALF_K_VALUES.end()),
      1, smoke);
}

}  // namespace characterise

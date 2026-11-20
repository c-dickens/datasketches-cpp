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

// Profiles update_speed and call_speed: the speed of the MinHash sketch, with theta at the same k
// for comparison. The items are made before any timer starts. Every result goes into a sink,
// which is printed to stderr at the end, so that the compiler cannot remove the timed work.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "characterise/common.hpp"
#include "characterise/profiles.hpp"
#include "minhash_sketch.hpp"
#include "theta_jaccard_similarity.hpp"
#include "theta_sketch.hpp"
#include "theta_union.hpp"

namespace characterise {

namespace {

using steady = std::chrono::steady_clock;

constexpr uint64_t FULL_MAX_N = 1ULL << 20;
constexpr uint64_t SMOKE_MAX_N = 1ULL << 14;
constexpr unsigned FULL_PPO = 8;
constexpr unsigned SMOKE_PPO = 4;
constexpr unsigned FULL_LG_MAX_N = 20;
constexpr unsigned SMOKE_LG_MAX_N = 14;
constexpr unsigned FULL_LG_MIN_TRIALS = 4;
constexpr unsigned FULL_LG_MAX_TRIALS = 16;
constexpr unsigned SMOKE_LG_MIN_TRIALS = 2;
constexpr unsigned SMOKE_LG_MAX_TRIALS = 12;
constexpr uint64_t FULL_CALLS = 1ULL << 16;
constexpr uint64_t SMOKE_CALLS = 1ULL << 10;
constexpr uint64_t WARMUP_CALLS = 1ULL << 10;
constexpr uint64_t INPUT_ITEMS = 1ULL << 16;  // A = [0, 2^16), B = [2^15, 3 * 2^15)

uint8_t lg_k(uint16_t k) { return static_cast<uint8_t>(std::lround(std::log2(k))); }

double elapsed_ns(steady::time_point start, steady::time_point stop) {
  return std::chrono::duration<double, std::nano>(stop - start).count();
}

struct minhash_ops {
  using sketch = datasketches::minhash_sketch;
  static constexpr const char* NAME = "minhash";
  static sketch make(uint16_t k) { return sketch::builder().set_k(k).build(); }
  static double value(const sketch& s) { return static_cast<double>(s.get_signature()[0]); }
  // Merge is in place, so repeated merges of the same B into one copy of A have the same cost.
  static double merge(sketch& a_copy, const sketch& /*a*/, const sketch& b, uint16_t /*k*/) {
    a_copy.merge(b);
    return value(a_copy);
  }
  static double jaccard(const sketch& a, const sketch& b) { return a.jaccard(b); }
};

struct theta_ops {
  using sketch = datasketches::update_theta_sketch;
  static constexpr const char* NAME = "theta";
  static sketch make(uint16_t k) { return sketch::builder().set_lg_k(lg_k(k)).build(); }
  static double value(const sketch& s) { return static_cast<double>(s.get_num_retained()); }
  // A user who merges two sketches builds a new union, so each call does that.
  static double merge(sketch& /*a_copy*/, const sketch& a, const sketch& b, uint16_t k) {
    auto u = datasketches::theta_union::builder().set_lg_k(lg_k(k)).build();
    u.update(a);
    u.update(b);
    return static_cast<double>(u.get_result().get_num_retained());
  }
  static double jaccard(const sketch& a, const sketch& b) {
    return datasketches::theta_jaccard_similarity::jaccard(a, b)[1];
  }
};

struct update_grid {
  std::vector<uint64_t> ns;
  unsigned lg_max_n;
  unsigned lg_min_trials;
  unsigned lg_max_trials;
};

template<typename Ops, typename Item>
void update_rows(std::ostream& out, uint16_t k, const char* type, const std::vector<Item>& items,
                 const update_grid& grid, double& sink) {
  for (const uint64_t n : grid.ns) {
    const uint64_t trials = num_trials(n, 0, grid.lg_max_n, grid.lg_min_trials, grid.lg_max_trials);
    {
      auto warm_up = Ops::make(k);
      for (uint64_t i = 0; i < n; ++i) {
        warm_up.update(items[i]);
      }
      sink += Ops::value(warm_up);
    }
    double total_ns = 0.0;
    double min_ns = std::numeric_limits<double>::max();
    double max_ns = 0.0;
    for (uint64_t t = 0; t < trials; ++t) {
      auto s = Ops::make(k);
      const auto start = steady::now();
      for (uint64_t i = 0; i < n; ++i) {
        s.update(items[i]);
      }
      const double ns = elapsed_ns(start, steady::now());
      sink += Ops::value(s);
      total_ns += ns;
      min_ns = std::min(min_ns, ns);
      max_ns = std::max(max_ns, ns);
    }
    const auto nd = static_cast<double>(n);
    out << Ops::NAME << "\t" << k << "\t" << type << "\t" << n << "\t" << trials << "\t"
        << total_ns / (static_cast<double>(trials) * nd) << "\t" << min_ns / nd << "\t" << max_ns / nd << "\n";
  }
}

template<typename Ops>
void update_sketch(std::ostream& out, const std::vector<uint16_t>& k_values, const update_grid& grid,
                   const std::vector<uint64_t>& u64, const std::vector<std::string>& str8,
                   const std::vector<std::string>& str64, double& sink) {
  const auto start = steady::now();
  for (const uint16_t k : k_values) {
    update_rows<Ops>(out, k, "u64", u64, grid, sink);
    update_rows<Ops>(out, k, "str8", str8, grid, sink);
    update_rows<Ops>(out, k, "str64", str64, grid, sink);
    std::cerr << "update_speed: " << Ops::NAME << " k = " << k << " done, " << std::fixed << std::setprecision(1)
              << elapsed_ns(start, steady::now()) * 1e-9 << " s elapsed\n";
  }
}

// The mean ns of `calls` calls of f, after WARMUP_CALLS untimed calls.
template<typename F>
double mean_ns_per_call(uint64_t calls, F f, double& sink) {
  for (uint64_t c = 0; c < WARMUP_CALLS; ++c) {
    sink += f();
  }
  const auto start = steady::now();
  for (uint64_t c = 0; c < calls; ++c) {
    sink += f();
  }
  return elapsed_ns(start, steady::now()) / static_cast<double>(calls);
}

template<typename Ops>
void call_sketch(std::ostream& out, const std::vector<uint16_t>& k_values, uint64_t calls, double& sink) {
  for (const uint16_t k : k_values) {
    auto a = Ops::make(k);
    auto b = Ops::make(k);
    for (uint64_t i = 0; i < INPUT_ITEMS; ++i) {
      a.update(i);
      b.update(i + INPUT_ITEMS / 2);
    }
    auto a_copy = a;
    const double construct = mean_ns_per_call(calls, [&] { return Ops::value(Ops::make(k)); }, sink);
    const double merge = mean_ns_per_call(calls, [&] { return Ops::merge(a_copy, a, b, k); }, sink);
    const double jaccard = mean_ns_per_call(calls, [&] { return Ops::jaccard(a, b); }, sink);
    out << Ops::NAME << "\t" << k << "\tconstruct\t" << calls << "\t" << construct << "\n";
    out << Ops::NAME << "\t" << k << "\tmerge\t" << calls << "\t" << merge << "\n";
    out << Ops::NAME << "\t" << k << "\tjaccard\t" << calls << "\t" << jaccard << "\n";
    std::cerr << "call_speed: " << Ops::NAME << " k = " << k << " done\n";
  }
}

std::vector<uint16_t> k_values_of(bool smoke) {
  return smoke ? std::vector<uint16_t>(SMOKE_K_VALUES.begin(), SMOKE_K_VALUES.end())
               : std::vector<uint16_t>(K_VALUES.begin(), K_VALUES.end());
}

}  // namespace

void update_speed(std::ostream& out, bool smoke) {
  const uint64_t max_n = smoke ? SMOKE_MAX_N : FULL_MAX_N;
  const update_grid grid{pwr_2_law_points(1, max_n, smoke ? SMOKE_PPO : FULL_PPO),
                         smoke ? SMOKE_LG_MAX_N : FULL_LG_MAX_N,
                         smoke ? SMOKE_LG_MIN_TRIALS : FULL_LG_MIN_TRIALS,
                         smoke ? SMOKE_LG_MAX_TRIALS : FULL_LG_MAX_TRIALS};
  const auto k_values = k_values_of(smoke);
  const auto u64 = u64_items(max_n);
  const auto str8 = string_items(max_n, 8);
  const auto str64 = string_items(max_n, 64);

  out << std::setprecision(10);
  write_metadata(out, smoke ? "minhash-update-speed-smoke" : "minhash-update-speed");
  out << "# cpu=" << cpu_model() << "\n";
  out << "Sketch\tK\tItemType\tN\tTrials\tMeanNsPerItem\tMinNsPerItem\tMaxNsPerItem\n";

  double sink = 0.0;
  update_sketch<minhash_ops>(out, k_values, grid, u64, str8, str64, sink);
  update_sketch<theta_ops>(out, k_values, grid, u64, str8, str64, sink);
  std::cerr << "update_speed: sink = " << sink << "\n";
}

void call_speed(std::ostream& out, bool smoke) {
  const auto k_values = k_values_of(smoke);
  const uint64_t calls = smoke ? SMOKE_CALLS : FULL_CALLS;

  out << std::setprecision(10);
  write_metadata(out, smoke ? "minhash-call-speed-smoke" : "minhash-call-speed");
  out << "# cpu=" << cpu_model() << "\n";
  out << "Sketch\tK\tOperation\tCalls\tMeanNsPerCall\n";

  double sink = 0.0;
  call_sketch<minhash_ops>(out, k_values, calls, sink);
  call_sketch<theta_ops>(out, k_values, calls, sink);
  std::cerr << "call_speed: sink = " << sink << "\n";
}

}  // namespace characterise

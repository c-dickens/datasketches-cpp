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

#ifndef CHARACTERISE_COMMON_HPP_
#define CHARACTERISE_COMMON_HPP_

// Fork-only characterisation code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Shared helpers of the characterisation profiles: the constants, the TSV metadata and the
// quantile columns, in the style of apache/datasketches-characterization.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "kll_sketch.hpp"
#include "theta_sketch.hpp"

namespace characterise {

constexpr std::array<double, 9> QUANTILES = {0.0, 0.00135, 0.02275, 0.15866, 0.5, 0.84134, 0.97725, 0.99865, 1.0};
constexpr std::array<uint16_t, 5> K_VALUES = {16, 64, 256, 1024, 4096};
constexpr std::array<uint16_t, 3> SMOKE_K_VALUES = {16, 64, 256};
constexpr uint16_t KLL_K = 10000;

/// Writes "# profile=<name>", "# git_commit=<sha>", "# compiler=<version>" and
/// "# build_flags=<flags>", one per line. The commit and the flags come from compile
/// definitions that CMake sets (git rev-parse HEAD at configure time, and
/// CMAKE_CXX_FLAGS plus the flags of the build type).
void write_metadata(std::ostream& out, const std::string& profile);

/// "<prefix>_Q0\t<prefix>_Q0.00135\t...\t<prefix>_Q1", one name for each of QUANTILES.
std::string quantile_header(const std::string& prefix);

/// The 9 quantiles of the sketch at QUANTILES, tab-separated, with no leading or trailing tab.
void write_quantiles(std::ostream& out, const datasketches::kll_sketch<double>& sketch);

/// (estimate - j) / sqrt(j (1 - j) / k). Throws std::invalid_argument unless 0 < j < 1.
double normalised_error(double estimate, double j, uint16_t k);

/// True when bounds[0] <= j <= bounds[2].
bool covers(const std::array<double, 3>& bounds, double j);

/// The points start, then each next point is the first point of the grid round(2^(i / ppo)),
/// i = 0, 1, ..., that is above the current point, up to and including end. This is
/// pwr_2_law_next of the characterisation repo. The rule cur -> round(2^(log2(cur) + 1/ppo))
/// is not used: it leaves the grid, and from 2 at ppo = 16 it ends at 65292, not 65536.
/// Throws std::invalid_argument if start is 0, end < start, or ppo is 0.
std::vector<uint64_t> pwr_2_law_points(uint64_t start, uint64_t end, unsigned ppo);

/// True when item x is in the B set for J = m/10: ((x + 1) m) / 10 > (x m) / 10, in integer
/// arithmetic. The first n items then hold exactly (n m) / 10 members of B.
bool in_b(uint64_t x, unsigned m);

/// The size in bytes of a.compact().serialize() without compression:
/// estimation mode: 24 + 8 r; exact mode with r = 1: 16; exact mode otherwise: 16 + 8 r,
/// where r = a.get_num_retained().
uint64_t theta_compact_bytes(const datasketches::update_theta_sketch& a);

/// True when the two key lists are equal in at least one position (band).
/// Throws std::invalid_argument if the sizes differ.
bool lsh_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b);

/// 1 - (1 - j^r)^b.
double lsh_theory_rate(double j, unsigned r, unsigned b);

/// round(2^lg_t), with lg_t = lg_max_t - (lg_max_t - lg_min_t) * (log2(x) - lg_min_x) / (lg_max_x - lg_min_x),
/// and lg_t limited to [lg_min_t, lg_max_t]. This is get_num_trials of the characterisation repo.
uint64_t num_trials(uint64_t x, unsigned lg_min_x, unsigned lg_max_x, unsigned lg_min_t, unsigned lg_max_t);

/// The CPU model: on macOS from sysctlbyname("machdep.cpu.brand_string"), on Linux from the
/// first "model name" line of /proc/cpuinfo, otherwise "unknown".
std::string cpu_model();

/// The items of one type, made once: "u64" gives i * 0x9E3779B97F4A7C15 (mod 2^64) as uint64_t;
/// "str8" and "str64" give strings of 8 and 64 bytes: the 8 little-endian bytes of i, then
/// 'x' up to the length.
std::vector<uint64_t> u64_items(std::size_t n);
std::vector<std::string> string_items(std::size_t n, std::size_t length);

/// The bytes that all counting_allocator instances hold now (allocated minus deallocated).
int64_t counted_bytes();

namespace detail {
// The counter of counting_allocator. The profiles are single-threaded, so it is a plain integer.
inline int64_t counted_bytes_total = 0;
}  // namespace detail

/// A std::allocator that adds the size of each allocation to one global counter, and
/// subtracts it on deallocation. It has a rebind, and all instances compare equal.
template<typename T>
class counting_allocator {
 public:
  using value_type = T;
  template<typename U>
  struct rebind {
    using other = counting_allocator<U>;
  };

  counting_allocator() = default;
  template<typename U>
  counting_allocator(const counting_allocator<U>& /*other*/) noexcept {}  // NOLINT(google-explicit-constructor): allocators convert implicitly on rebind

  T* allocate(std::size_t n) {
    T* p = std::allocator<T>().allocate(n);
    detail::counted_bytes_total += static_cast<int64_t>(n * sizeof(T));
    return p;
  }

  void deallocate(T* p, std::size_t n) noexcept {
    detail::counted_bytes_total -= static_cast<int64_t>(n * sizeof(T));
    std::allocator<T>().deallocate(p, n);
  }

  template<typename U>
  bool operator==(const counting_allocator<U>& /*other*/) const noexcept { return true; }
  template<typename U>
  bool operator!=(const counting_allocator<U>& /*other*/) const noexcept { return false; }
};

/// One planted or unrelated document of the LSH performance data.
struct lsh_document {
  uint64_t id;
  int64_t group;   // -1 for an unrelated document
  double true_j;   // the exact J with the other members of its group; 0 for unrelated
};

/// The items of one document: 1000.
constexpr uint64_t LSH_DOCUMENT_ITEMS = 1000;

/// The layout of n documents. The first n / 10 ids are the planted part, in groups in id order.
/// Group g has h = XXHash64::hash(&g, 8, 2), size 2 + (h >> 32) mod 4 (cut to the planted ids
/// that are left), and target J* = 0.6 + 0.35 (h mod 1000) / 999. If fewer than 2 planted ids
/// are left for the last group, they are unrelated documents. true_j is the exact J of the
/// items, (1000 - 2 delta) / (1000 + 2 delta), with delta = lsh_group_delta(g). Deterministic.
std::vector<lsh_document> lsh_documents(uint64_t n);

/// delta = round(1000 (1 - J*) / (2 (1 + J*))) of group g: the items that each member drops
/// from the base set, and the fresh items that it adds.
uint64_t lsh_group_delta(int64_t group);

/// Calls f(item) for each of the 1000 items of document d of the layout.
/// Unrelated document d: [d 2^11, d 2^11 + 1000). Member m of the group with first id f:
/// [f 2^11, f 2^11 + 1000) without [f 2^11 + m delta, f 2^11 + (m + 1) delta), and the fresh
/// items [2^40 + d 2^11, 2^40 + d 2^11 + delta).
template<typename F>
void lsh_document_items(const std::vector<lsh_document>& docs, uint64_t d, F f) {
  constexpr uint64_t SPACING = 1ULL << 11;
  constexpr uint64_t FRESH = 1ULL << 40;
  const int64_t group = docs[d].group;
  if (group < 0) {
    for (uint64_t i = 0; i < LSH_DOCUMENT_ITEMS; ++i) {
      f(d * SPACING + i);
    }
    return;
  }
  uint64_t first = d;
  while (first > 0 && docs[first - 1].group == group) {
    --first;
  }
  const uint64_t m = d - first;
  const uint64_t delta = lsh_group_delta(group);
  const uint64_t base = first * SPACING;
  for (uint64_t i = 0; i < LSH_DOCUMENT_ITEMS; ++i) {
    if (i < m * delta || i >= (m + 1) * delta) {
      f(base + i);
    }
  }
  for (uint64_t i = 0; i < delta; ++i) {
    f(FRESH + d * SPACING + i);
  }
}

/// The `count` ids with the smallest XXHash64::hash(&id, 8, seed), in ascending id order.
/// Ties of the hash go to the smaller id. If count >= ids.size(), returns all ids, sorted.
std::vector<uint64_t> smallest_hash_ids(std::vector<uint64_t> ids, std::size_t count, uint64_t seed);

/// The bucket counts of the LSH bands of n documents.
struct lsh_bucket_counts {
  uint64_t candidate_pairs;  // distinct pairs that share at least one bucket
  uint64_t band_collisions;  // sum over bands and buckets of s (s - 1) / 2: once for each band a pair shares
  uint64_t largest_bucket;   // the largest s
};

/// The bucket counts of `keys`, which holds the b band keys of each document in turn
/// (document d at [d b, (d + 1) b)). b must be at least 1.
lsh_bucket_counts count_lsh_buckets(const std::vector<uint64_t>& keys, std::size_t b);

}  // namespace characterise

#endif  // CHARACTERISE_COMMON_HPP_

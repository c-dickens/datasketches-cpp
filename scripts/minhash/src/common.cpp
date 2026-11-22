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

#include "characterise/common.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "xxhash64.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace characterise {

void write_metadata(std::ostream& out, const std::string& profile) {
  out << "# profile=" << profile << "\n";
  out << "# git_commit=" << CHARACTERISE_GIT_COMMIT << "\n";
  out << "# compiler=" << CHARACTERISE_COMPILER << "\n";
  out << "# build_flags=" << CHARACTERISE_BUILD_FLAGS << "\n";
}

std::string quantile_header(const std::string& prefix) {
  std::ostringstream s;
  for (std::size_t q = 0; q < QUANTILES.size(); ++q) {
    s << (q == 0 ? "" : "\t") << prefix << "_Q" << QUANTILES[q];
  }
  return s.str();
}

void write_quantiles(std::ostream& out, const datasketches::kll_sketch<double>& sketch) {
  for (std::size_t q = 0; q < QUANTILES.size(); ++q) {
    out << (q == 0 ? "" : "\t") << sketch.get_quantile(QUANTILES[q]);
  }
}

double normalised_error(double estimate, double j, uint16_t k) {
  if (!(j > 0.0 && j < 1.0)) {
    throw std::invalid_argument("normalised_error: j must be in (0, 1)");
  }
  return (estimate - j) / std::sqrt(j * (1.0 - j) / k);
}

bool covers(const std::array<double, 3>& bounds, double j) {
  return bounds[0] <= j && j <= bounds[2];
}

std::vector<uint64_t> pwr_2_law_points(uint64_t start, uint64_t end, unsigned ppo) {
  if (start == 0 || end < start || ppo == 0) {
    throw std::invalid_argument("pwr_2_law_points: need 0 < start <= end and ppo > 0");
  }
  std::vector<uint64_t> points{start};
  for (uint64_t cur = start;;) {
    // pwr_2_law_next: the next point of the grid round(2^(i / ppo)) that is above cur. Start at
    // floor, not round: every i <= floor(ppo log2(cur)) gives a point <= cur, so no point is skipped.
    auto i = static_cast<int64_t>(std::floor(std::log2(static_cast<double>(cur)) * ppo));
    uint64_t next = 0;
    do {
      next = static_cast<uint64_t>(std::round(std::pow(2.0, static_cast<double>(++i) / ppo)));
    } while (next <= cur);
    if (next > end) {
      break;
    }
    points.push_back(next);
    cur = next;
  }
  return points;
}

bool in_b(uint64_t x, unsigned m) {
  return (x + 1) * m / 10 > x * m / 10;
}

uint64_t theta_compact_bytes(const datasketches::update_theta_sketch& a) {
  const uint64_t r = a.get_num_retained();
  if (a.is_estimation_mode()) {
    return 24 + 8 * r;
  }
  return r == 1 ? 16 : 16 + 8 * r;
}

bool lsh_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b) {
  if (keys_a.size() != keys_b.size()) {
    throw std::invalid_argument("lsh_candidate: the key lists differ in size");
  }
  for (std::size_t band = 0; band < keys_a.size(); ++band) {
    if (keys_a[band] == keys_b[band]) {
      return true;
    }
  }
  return false;
}

double lsh_theory_rate(double j, unsigned r, unsigned b) {
  // -expm1(b log1p(-j^r)) is 1 - (1 - j^r)^b without the cancellation when j^r is small.
  return -std::expm1(static_cast<double>(b) * std::log1p(-std::pow(j, r)));
}

uint64_t num_trials(uint64_t x, unsigned lg_min_x, unsigned lg_max_x, unsigned lg_min_t, unsigned lg_max_t) {
  const double slope = (static_cast<double>(lg_max_t) - lg_min_t) / (static_cast<double>(lg_max_x) - lg_min_x);
  const double lg_t = lg_max_t - slope * (std::log2(static_cast<double>(x)) - lg_min_x);
  return static_cast<uint64_t>(std::round(std::exp2(std::clamp<double>(lg_t, lg_min_t, lg_max_t))));
}

std::string cpu_model() {
#if defined(__APPLE__)
  char brand[256] = {};
  std::size_t size = sizeof(brand);
  if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0 && brand[0] != '\0') {
    return brand;
  }
#elif defined(__linux__)
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    const auto colon = line.find(':');
    if (line.rfind("model name", 0) == 0 && colon != std::string::npos && colon + 2 <= line.size()) {
      return line.substr(colon + 2);
    }
  }
#endif
  return "unknown";
}

std::vector<uint64_t> u64_items(std::size_t n) {
  std::vector<uint64_t> items(n);
  for (std::size_t i = 0; i < n; ++i) {
    items[i] = static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
  }
  return items;
}

std::vector<std::string> string_items(std::size_t n, std::size_t length) {
  std::vector<std::string> items(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::string s(8, '\0');
    for (std::size_t b = 0; b < 8; ++b) {
      s[b] = static_cast<char>((static_cast<uint64_t>(i) >> (8 * b)) & 0xFF);
    }
    s.resize(length, 'x');
    items[i] = std::move(s);
  }
  return items;
}

int64_t counted_bytes() { return detail::counted_bytes_total; }

namespace {

constexpr uint64_t GROUP_SEED = 2;

uint64_t group_hash(int64_t group) {
  const auto g = static_cast<uint64_t>(group);
  return XXHash64::hash(&g, sizeof(g), GROUP_SEED);
}

}  // namespace

uint64_t lsh_group_delta(int64_t group) {
  const double target_j = 0.6 + 0.35 * static_cast<double>(group_hash(group) % 1000) / 999.0;
  return static_cast<uint64_t>(
      std::llround(static_cast<double>(LSH_DOCUMENT_ITEMS) * (1.0 - target_j) / (2.0 * (1.0 + target_j))));
}

std::vector<lsh_document> lsh_documents(uint64_t n) {
  std::vector<lsh_document> docs;
  docs.reserve(n);
  const uint64_t planted = n / 10;
  int64_t group = 0;
  while (planted - docs.size() >= 2) {
    const uint64_t left = planted - docs.size();
    const uint64_t size = std::min<uint64_t>(2 + (group_hash(group) >> 32) % 4, left);
    const auto items = static_cast<double>(LSH_DOCUMENT_ITEMS);
    const auto delta = static_cast<double>(lsh_group_delta(group));
    const double true_j = (items - 2.0 * delta) / (items + 2.0 * delta);
    for (uint64_t m = 0; m < size; ++m) {
      docs.push_back({docs.size(), group, true_j});
    }
    ++group;
  }
  while (docs.size() < n) {
    docs.push_back({docs.size(), -1, 0.0});
  }
  return docs;
}

std::vector<uint64_t> smallest_hash_ids(std::vector<uint64_t> ids, std::size_t count, uint64_t seed) {
  if (count < ids.size()) {
    std::vector<std::pair<uint64_t, uint64_t>> keyed;
    keyed.reserve(ids.size());
    for (const uint64_t id : ids) {
      keyed.emplace_back(XXHash64::hash(&id, sizeof(id), seed), id);
    }
    std::sort(keyed.begin(), keyed.end());
    ids.clear();
    for (std::size_t i = 0; i < count; ++i) {
      ids.push_back(keyed[i].second);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

lsh_bucket_counts count_lsh_buckets(const std::vector<uint64_t>& keys, std::size_t b) {
  lsh_bucket_counts counts{0, 0, 0};
  const std::size_t n = keys.size() / b;
  // shortcut: keeps every colliding (pair, band); fine while buckets are small, as in the
  // planted data. Count the pairs per document with query_candidates() if buckets get large.
  std::vector<std::pair<uint64_t, uint64_t>> pairs;
  for (std::size_t band = 0; band < b; ++band) {
    // The map gives only counts and pairs that are sorted below, so its order does not change the result.
    std::unordered_map<uint64_t, std::vector<uint64_t>> buckets;
    for (std::size_t d = 0; d < n; ++d) {
      buckets[keys[d * b + band]].push_back(d);
    }
    for (const auto& bucket : buckets) {
      const std::vector<uint64_t>& ids = bucket.second;  // ascending, so each pair is (smaller, larger)
      counts.band_collisions += ids.size() * (ids.size() - 1) / 2;
      counts.largest_bucket = std::max<uint64_t>(counts.largest_bucket, ids.size());
      for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
          pairs.emplace_back(ids[i], ids[j]);
        }
      }
    }
  }
  std::sort(pairs.begin(), pairs.end());
  counts.candidate_pairs = static_cast<uint64_t>(std::unique(pairs.begin(), pairs.end()) - pairs.begin());
  return counts;
}

}  // namespace characterise

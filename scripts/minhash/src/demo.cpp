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

#include "demo/demo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>

#include "xxhash64.h"

namespace demo {

namespace {

const char* verdict(bool checked, bool pass) {
  if (!checked) {
    return "not checked";
  }
  return pass ? "PASS" : "FAIL";
}

std::string fixed(double value, int digits) {
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%.*f", digits, value);
  return buf.data();
}

}  // namespace

std::size_t jaccard_bin(double j) {
  const auto bin = static_cast<std::size_t>(std::floor(j * static_cast<double>(NUM_BINS)));
  return std::min(bin, NUM_BINS - 1);
}

std::vector<std::size_t> sample_indices(std::size_t n, std::size_t count, uint64_t seed) {
  if (count > n) {
    throw std::invalid_argument("sample count " + std::to_string(count) + " exceeds n " + std::to_string(n));
  }
  std::vector<std::pair<uint64_t, std::size_t>> keyed;
  keyed.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto value = static_cast<uint64_t>(i);
    keyed.emplace_back(XXHash64::hash(&value, sizeof(value), seed), i);
  }
  // pairs compare by hash first, then by index, which breaks ties by i
  std::partial_sort(keyed.begin(), keyed.begin() + static_cast<std::ptrdiff_t>(count), keyed.end());
  std::vector<std::size_t> result;
  result.reserve(count);
  for (std::size_t c = 0; c < count; ++c) {
    result.push_back(keyed[c].second);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::pair<std::size_t, std::size_t>> check_pairs(
    const std::vector<std::string>& keys, std::size_t sample_size, uint64_t seed) {
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  const auto sample = sample_indices(keys.size(), sample_size, seed);
  pairs.reserve(sample.size() * (sample.size() - (sample.empty() ? 0 : 1)) / 2);
  for (std::size_t a = 0; a < sample.size(); ++a) {
    for (std::size_t b = a + 1; b < sample.size(); ++b) {
      pairs.emplace_back(sample[a], sample[b]);
    }
  }
  std::map<std::string, std::vector<std::size_t>> by_key;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (!keys[i].empty()) {
      by_key[keys[i]].push_back(i);
    }
  }
  for (const auto& entry : by_key) {
    const auto& members = entry.second;  // ascending, because i ascends
    for (std::size_t a = 0; a < members.size(); ++a) {
      for (std::size_t b = a + 1; b < members.size(); ++b) {
        pairs.emplace_back(members[a], members[b]);
      }
    }
  }
  std::sort(pairs.begin(), pairs.end());
  pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
  return pairs;
}

datasketches::minhash_sketch sketch_of(const std::vector<std::string>& items, uint16_t k) {
  auto sketch = datasketches::minhash_sketch::builder().set_k(k).build();
  for (const auto& item : items) {
    sketch.update(item);
  }
  return sketch;
}

std::vector<bin_summary> summarise_bins(const std::vector<pair_estimate>& pairs, uint16_t k) {
  struct sums {
    std::size_t n = 0;
    double error = 0;
    double variance = 0;  // sum of J (1 - J) / k
    std::size_t covered2 = 0;
    std::size_t covered3 = 0;
  };
  std::array<sums, NUM_BINS> acc{};
  for (const auto& p : pairs) {
    if (p.exact <= 0.0 || p.exact >= 1.0) {
      continue;
    }
    auto& s = acc[jaccard_bin(p.exact)];
    ++s.n;
    s.error += p.estimate - p.exact;
    s.variance += p.exact * (1.0 - p.exact) / static_cast<double>(k);
    if (p.bounds2[0] <= p.exact && p.exact <= p.bounds2[2]) {
      ++s.covered2;
    }
    if (p.bounds3[0] <= p.exact && p.exact <= p.bounds3[2]) {
      ++s.covered3;
    }
  }
  // second pass: the squared deviations from the mean bias of each bin
  std::array<double, NUM_BINS> squares{};
  for (const auto& p : pairs) {
    if (p.exact <= 0.0 || p.exact >= 1.0) {
      continue;
    }
    const std::size_t bin = jaccard_bin(p.exact);
    const double mean = acc[bin].error / static_cast<double>(acc[bin].n);
    const double d = (p.estimate - p.exact) - mean;
    squares[bin] += d * d;
  }

  std::vector<bin_summary> result;
  result.reserve(NUM_BINS);
  for (std::size_t bin = 0; bin < NUM_BINS; ++bin) {
    const auto& s = acc[bin];
    const auto n = static_cast<double>(s.n);
    bin_summary out{};
    out.bin = bin;
    out.pairs = s.n;
    if (s.n > 0) {
      out.mean_bias = s.error / n;
      out.expected_sd = std::sqrt(s.variance / n);
      out.coverage2 = static_cast<double>(s.covered2) / n;
      out.coverage3 = static_cast<double>(s.covered3) / n;
    }
    if (s.n > 1) {
      out.observed_sd = std::sqrt(squares[bin] / (n - 1.0));
    }
    out.checked = s.n >= MIN_BIN_PAIRS;
    out.bias_pass = std::fabs(out.mean_bias) <= MAX_MEAN_BIAS;
    out.spread_pass = out.expected_sd > 0.0 &&
        std::fabs(out.observed_sd / out.expected_sd - 1.0) <= SPREAD_TOLERANCE;
    out.coverage2_pass = out.coverage2 >= NOMINAL_COVERAGE_2SD - COVERAGE_SLACK;
    out.coverage3_pass = out.coverage3 >= NOMINAL_COVERAGE_3SD - COVERAGE_SLACK;
    result.push_back(out);
  }
  return result;
}

bool bin_passes(const bin_summary& bin) {
  return !bin.checked || (bin.bias_pass && bin.spread_pass && bin.coverage2_pass && bin.coverage3_pass);
}

std::string bins_table(const std::vector<bin_summary>& bins) {
  std::ostringstream os;
  os << "| J bin | pairs | mean bias | observed SD | expected SD | SD ratio | coverage 2 SD | coverage 3 SD "
        "| bias | spread | coverage 2 SD | coverage 3 SD |\n";
  os << "|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|\n";
  for (const auto& b : bins) {
    const double low = static_cast<double>(b.bin) / static_cast<double>(NUM_BINS);
    const double high = static_cast<double>(b.bin + 1) / static_cast<double>(NUM_BINS);
    os << "| [" << fixed(low, 1) << ", " << fixed(high, 1) << ") | "
       << b.pairs << " | ";
    if (b.pairs == 0) {
      os << "- | - | - | - | - | - | ";
    } else {
      os << fixed(b.mean_bias, 5) << " | " << fixed(b.observed_sd, 5) << " | " << fixed(b.expected_sd, 5) << " | "
         << (b.expected_sd > 0.0 ? fixed(b.observed_sd / b.expected_sd, 3) : std::string("-")) << " | "
         << fixed(b.coverage2, 4) << " | " << fixed(b.coverage3, 4) << " | ";
    }
    os << verdict(b.checked, b.bias_pass) << " | " << verdict(b.checked, b.spread_pass) << " | "
       << verdict(b.checked, b.coverage2_pass) << " | " << verdict(b.checked, b.coverage3_pass) << " |\n";
  }
  return os.str();
}

}  // namespace demo

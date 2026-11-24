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
#include <istream>
#include <map>
#include <ostream>
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

double candidate_probability(double j, unsigned rows, unsigned bands) {
  return 1.0 - std::pow(1.0 - std::pow(j, static_cast<double>(rows)), static_cast<double>(bands));
}

bool is_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b) {
  if (keys_a.size() != keys_b.size()) {
    throw std::invalid_argument("key lists differ in size: " + std::to_string(keys_a.size()) + " and " +
                                std::to_string(keys_b.size()));
  }
  for (std::size_t i = 0; i < keys_a.size(); ++i) {
    if (keys_a[i] == keys_b[i]) {
      return true;
    }
  }
  return false;
}

double poisson_binomial_p_value(const std::vector<double>& probs, std::size_t observed) {
  if (observed > probs.size()) {
    return 0.0;
  }
  // The two-sided p-value of the count of misses (Bernoulli(1 - p_i)) at n - observed is the same,
  // so the program counts the smaller side.
  const bool flip = 2 * observed > probs.size();
  const std::size_t o = flip ? probs.size() - observed : observed;
  // dist[x] = P(X = x) for x <= o, and dist[o + 1] = P(X > o): O(n o) instead of O(n^2)
  std::vector<double> dist(o + 2, 0.0);
  dist[0] = 1.0;
  std::size_t seen = 0;
  for (const double prob : probs) {
    const double p = flip ? 1.0 - prob : prob;
    if (p == 0.0) {
      continue;  // a Bernoulli(0) does not change the distribution
    }
    ++seen;
    dist[o + 1] += dist[o] * p;
    for (std::size_t x = std::min(seen, o); x > 0; --x) {
      dist[x] = dist[x] * (1.0 - p) + dist[x - 1] * p;
    }
    dist[0] *= 1.0 - p;
  }
  double lower = 0.0;  // P(X <= o)
  for (std::size_t x = 0; x <= o; ++x) {
    lower += dist[x];
  }
  const double upper = dist[o] + dist[o + 1];  // P(X >= o)
  return std::min(1.0, 2.0 * std::min(lower, upper));
}

scurve_counter::scurve_counter(unsigned rows, unsigned bands) : rows_(rows), bands_(bands) {}

void scurve_counter::add(double exact, bool candidate) {
  const std::size_t bin = jaccard_bin(exact);
  probs_[bin].push_back(candidate_probability(exact, rows_, bands_));
  if (candidate) {
    ++candidates_[bin];
  }
}

std::vector<scurve_bin> scurve_counter::bins() const {
  std::vector<scurve_bin> result;
  result.reserve(NUM_BINS);
  for (std::size_t bin = 0; bin < NUM_BINS; ++bin) {
    const auto& probs = probs_[bin];
    scurve_bin out{};
    out.bin = bin;
    out.pairs = probs.size();
    out.candidates = candidates_[bin];
    for (const double p : probs) {
      out.expected += p;
    }
    out.p_value = poisson_binomial_p_value(probs, out.candidates);
    out.filled = out.pairs >= MIN_BIN_PAIRS;
    out.pass = out.filled && out.p_value >= SCURVE_ALPHA / static_cast<double>(NUM_BINS);
    result.push_back(out);
  }
  return result;
}

bool scurve_passes(const std::vector<scurve_bin>& bins) {
  return std::all_of(bins.begin(), bins.end(), [](const scurve_bin& b) { return b.pass; });
}

std::vector<scurve_bin> scurve_check(const band_key_function& keys_of, unsigned rows, unsigned bands) {
  const auto k = static_cast<uint16_t>(rows * bands);
  const uint64_t bin_width = SCURVE_UNION / NUM_BINS;  // 100 values of c in each bin
  scurve_counter counter(rows, bands);
  uint64_t pair = 0;
  for (uint64_t bin = 0; bin < NUM_BINS; ++bin) {
    for (uint64_t t = 0; t < SCURVE_PAIRS_PER_BIN; ++t, ++pair) {
      const uint64_t c = bin * bin_width + 2 * (t % (bin_width / 2));  // even, so that a = b
      const uint64_t a = (SCURVE_UNION - c) / 2;
      const uint64_t o = pair << 32;
      auto s1 = datasketches::minhash_sketch::builder().set_k(k).build();
      auto s2 = datasketches::minhash_sketch::builder().set_k(k).build();
      for (uint64_t x = o; x < o + a + c; ++x) {
        s1.update(x);
      }
      for (uint64_t x = o + a; x < o + 2 * a + c; ++x) {
        s2.update(x);
      }
      counter.add(static_cast<double>(c) / static_cast<double>(SCURVE_UNION), is_candidate(keys_of(s1), keys_of(s2)));
    }
  }
  return counter.bins();
}

std::string scurve_table(const std::vector<scurve_bin>& bins) {
  std::ostringstream os;
  os << "| J bin | pairs | candidates | expected | candidate rate | expected rate | p-value | check |\n";
  os << "|---|---:|---:|---:|---:|---:|---:|---|\n";
  for (const auto& b : bins) {
    const double low = static_cast<double>(b.bin) / static_cast<double>(NUM_BINS);
    const double high = static_cast<double>(b.bin + 1) / static_cast<double>(NUM_BINS);
    // the last bin holds J = 1, so it is closed
    os << "| [" << fixed(low, 1) << ", " << fixed(high, 1) << (b.bin + 1 == NUM_BINS ? "] | " : ") | ")
       << b.pairs << " | " << b.candidates << " | " << fixed(b.expected, 2) << " | ";
    if (b.pairs == 0) {
      os << "- | - | ";
    } else {
      const auto n = static_cast<double>(b.pairs);
      os << fixed(static_cast<double>(b.candidates) / n, 6) << " | " << fixed(b.expected / n, 6) << " | ";
    }
    std::array<char, 32> p{};
    std::snprintf(p.data(), p.size(), "%.3g", b.p_value);
    os << p.data() << " | " << (b.pass ? "PASS" : (b.filled ? "FAIL" : "FAIL (bin not filled)")) << " |\n";
  }
  return os.str();
}

void pair_counts::add(bool is_similar, bool is_verified) {
  if (is_similar) {
    ++similar;
  }
  if (is_verified) {
    ++verified;
  }
  if (is_similar && is_verified) {
    ++both;
  }
}

std::vector<bucket> largest_buckets(const std::vector<std::vector<uint64_t>>& keys_per_item, std::size_t count) {
  // std::map keeps (band, key) order, so ties break by band, then key
  std::map<std::pair<uint16_t, uint64_t>, std::vector<std::size_t>> by_key;
  for (std::size_t i = 0; i < keys_per_item.size(); ++i) {
    const auto& keys = keys_per_item[i];
    for (std::size_t band = 0; band < keys.size(); ++band) {
      by_key[{static_cast<uint16_t>(band), keys[band]}].push_back(i);  // ascending, because i ascends
    }
  }
  std::vector<bucket> all;
  all.reserve(by_key.size());
  for (auto& entry : by_key) {
    all.push_back(bucket{entry.first.first, entry.first.second, std::move(entry.second)});
  }
  // stable: equal sizes keep the (band, key) order of the map
  std::stable_sort(all.begin(), all.end(),
                   [](const bucket& a, const bucket& b) { return a.items.size() > b.items.size(); });
  all.resize(std::min(count, all.size()));
  return all;
}

std::vector<grid_point> full_grid() {
  std::vector<grid_point> grid;
  for (const uint16_t k : FULL_K_VALUES) {
    for (const unsigned rows : FULL_ROWS) {
      for (const double t : FULL_THRESHOLDS) {
        grid.push_back(grid_point{k, rows, k / rows, t});
      }
    }
  }
  return grid;
}

std::size_t min_matches(double t, uint16_t k) {
  if (!(t > 0.0) || t > 1.0) {
    throw std::invalid_argument("threshold must be in (0, 1]");
  }
  std::size_t m = 0;
  while (static_cast<double>(m) / static_cast<double>(k) < t) {
    ++m;
  }
  return m;
}

std::vector<double> binomial_pmf(double j, std::size_t k) {
  std::vector<double> pmf(k + 1, 0.0);
  if (j <= 0.0) {
    pmf[0] = 1.0;
    return pmf;
  }
  if (j >= 1.0) {
    pmf[k] = 1.0;
    return pmf;
  }
  const double log_j = std::log(j);
  const double log_miss = std::log1p(-j);
  double log_choose = 0.0;  // log C(k, s), updated from s to s + 1
  for (std::size_t s = 0; s <= k; ++s) {
    pmf[s] = std::exp(log_choose + static_cast<double>(s) * log_j + static_cast<double>(k - s) * log_miss);
    log_choose += std::log(static_cast<double>(k - s)) - std::log(static_cast<double>(s + 1));
  }
  return pmf;
}

std::vector<double> full_band_given_matches(unsigned rows, unsigned bands) {
  const std::size_t k = static_cast<std::size_t>(rows) * bands;
  std::vector<double> log_fact(k + 1, 0.0);
  for (std::size_t n = 1; n <= k; ++n) {
    log_fact[n] = log_fact[n - 1] + std::log(static_cast<double>(n));
  }
  const auto log_choose = [&log_fact](std::size_t n, std::size_t x) {
    return log_fact[n] - log_fact[x] - log_fact[n - x];
  };
  // none[s] = P(no band so far is all-equal | s equal slots in those bands)
  std::vector<double> none(k + 1, 0.0);
  none[0] = 1.0;
  for (std::size_t band = 0; band < bands; ++band) {
    const std::size_t before = band * rows;  // slots of the bands so far
    const std::size_t after = before + rows;
    std::vector<double> next(k + 1, 0.0);
    for (std::size_t s = 0; s <= after; ++s) {
      // e equal slots in this band (e < rows, so it is not all-equal): hypergeometric given s
      for (std::size_t e = 0; e < rows && e <= s; ++e) {
        if (s - e > before || none[s - e] == 0.0) {
          continue;
        }
        const double h = std::exp(log_choose(rows, e) + log_choose(before, s - e) - log_choose(after, s));
        next[s] += h * none[s - e];
      }
    }
    none.swap(next);
  }
  std::vector<double> q(k + 1, 0.0);
  for (std::size_t s = 0; s <= k; ++s) {
    q[s] = std::max(0.0, 1.0 - none[s]);  // rounding can leave none[s] just above 1
  }
  return q;
}

std::vector<double> reported_tail(const std::vector<double>& pmf, const std::vector<double>& q) {
  if (pmf.size() != q.size()) {
    throw std::invalid_argument("pmf and q differ in size");
  }
  std::vector<double> tail(pmf.size() + 1, 0.0);
  for (std::size_t s = pmf.size(); s > 0; --s) {
    tail[s - 1] = tail[s] + pmf[s - 1] * q[s - 1];
  }
  return tail;
}

double reported_probability(double j, unsigned rows, unsigned bands, std::size_t min_matches) {
  const std::size_t k = static_cast<std::size_t>(rows) * bands;
  return reported_tail(binomial_pmf(j, k), full_band_given_matches(rows, bands)).at(min_matches);
}

pair_check check_pair_tests(const std::vector<double>& exact, const std::vector<double>& prob,
                            const std::vector<bool>& reported, std::size_t reported_disjoint, double t,
                            double test_alpha) {
  if (exact.size() != prob.size() || exact.size() != reported.size()) {
    throw std::invalid_argument("exact, prob and reported differ in size");
  }
  pair_check out{};
  std::vector<double> true_probs;
  std::vector<double> false_probs;
  for (std::size_t i = 0; i < exact.size(); ++i) {
    if (exact[i] >= t) {
      true_probs.push_back(prob[i]);
      out.expected_true += prob[i];
    } else if (exact[i] > 0.0) {
      false_probs.push_back(prob[i]);
      out.expected_false += prob[i];
    }
    out.counts.add(exact[i] >= t, reported[i]);
  }
  for (std::size_t d = 0; d < reported_disjoint; ++d) {
    out.counts.add(false, true);
  }
  out.false_pairs = false_probs.size();
  out.p_recall = poisson_binomial_p_value(true_probs, out.counts.both);
  out.p_precision = poisson_binomial_p_value(false_probs, out.counts.verified - out.counts.both);
  out.pass = out.p_recall >= test_alpha && out.p_precision >= test_alpha;
  return out;
}

std::vector<std::size_t> components(std::size_t n, const edge_list& edges) {
  std::vector<std::size_t> parent(n);
  for (std::size_t i = 0; i < n; ++i) {
    parent[i] = i;
  }
  const auto find = [&parent](std::size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  for (const auto& e : edges) {
    if (e.first >= n || e.second >= n) {
      throw std::invalid_argument("edge node out of range");
    }
    const std::size_t a = find(e.first);
    const std::size_t b = find(e.second);
    parent[std::max(a, b)] = std::min(a, b);  // the root is the smallest node
  }
  std::vector<std::size_t> label(n);
  for (std::size_t i = 0; i < n; ++i) {
    label[i] = find(i);
  }
  return label;
}

std::vector<std::vector<std::size_t>> clusters_of(const std::vector<std::size_t>& label) {
  std::vector<std::vector<std::size_t>> by_label(label.size());
  for (std::size_t i = 0; i < label.size(); ++i) {
    by_label[label[i]].push_back(i);  // ascending, because i ascends
  }
  std::vector<std::vector<std::size_t>> result;
  for (auto& members : by_label) {
    if (members.size() >= 2) {
      result.push_back(std::move(members));
    }
  }
  return result;
}

namespace {

std::size_t pairs_in(std::size_t n) {
  return n * (n - (n == 0 ? 0 : 1)) / 2;
}

// For each label of `outer` with 2 or more nodes: the number of distinct `inner` labels among its nodes.
std::vector<std::pair<std::size_t, std::size_t>> parts_of(const std::vector<std::size_t>& outer,
                                                          const std::vector<std::size_t>& inner) {
  std::vector<std::pair<std::size_t, std::size_t>> cells;
  cells.reserve(outer.size());
  for (std::size_t i = 0; i < outer.size(); ++i) {
    cells.emplace_back(outer[i], inner[i]);
  }
  std::sort(cells.begin(), cells.end());
  std::vector<std::pair<std::size_t, std::size_t>> result;  // (outer label, distinct inner labels)
  std::size_t nodes = 0;
  for (std::size_t c = 0; c < cells.size(); ++c) {
    const bool first_of_outer = c == 0 || cells[c].first != cells[c - 1].first;
    if (first_of_outer) {
      result.emplace_back(cells[c].first, 0);
      nodes = 0;
    }
    ++nodes;
    if (first_of_outer || cells[c].second != cells[c - 1].second) {
      ++result.back().second;
    }
    const bool last_of_outer = c + 1 == cells.size() || cells[c + 1].first != cells[c].first;
    if (last_of_outer && nodes < 2) {
      result.pop_back();
    }
  }
  return result;
}

}  // namespace

cluster_metrics compare_clusters(const std::vector<std::size_t>& lsh_label, const std::vector<std::size_t>& true_label) {
  if (lsh_label.size() != true_label.size()) {
    throw std::invalid_argument("label lists differ in size");
  }
  const std::size_t n = lsh_label.size();
  cluster_metrics out{};
  std::vector<std::size_t> lsh_size(n, 0);
  std::vector<std::size_t> true_size(n, 0);
  std::vector<std::pair<std::size_t, std::size_t>> cells;
  cells.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    ++lsh_size[lsh_label[i]];
    ++true_size[true_label[i]];
    cells.emplace_back(lsh_label[i], true_label[i]);
  }
  for (std::size_t i = 0; i < n; ++i) {
    out.lsh_clusters += lsh_size[i] >= 2 ? 1 : 0;
    out.true_clusters += true_size[i] >= 2 ? 1 : 0;
    out.lsh_pairs += pairs_in(lsh_size[i]);
    out.true_pairs += pairs_in(true_size[i]);
    out.largest_lsh = std::max(out.largest_lsh, lsh_size[i]);
    out.largest_true = std::max(out.largest_true, true_size[i]);
  }
  std::sort(cells.begin(), cells.end());
  for (std::size_t c = 0; c < cells.size();) {
    std::size_t end = c;
    while (end < cells.size() && cells[end] == cells[c]) {
      ++end;
    }
    out.both_pairs += pairs_in(end - c);
    c = end;
  }
  for (const auto& p : parts_of(lsh_label, true_label)) {
    out.merges += p.second >= 2 ? 1 : 0;
  }
  for (const auto& p : parts_of(true_label, lsh_label)) {
    out.splits += p.second >= 2 ? 1 : 0;
  }
  return out;
}

std::vector<merge_example> worst_merges(const std::vector<std::size_t>& lsh_label,
                                        const std::vector<std::size_t>& true_label, const edge_list& edges,
                                        const std::vector<double>& edge_j, std::size_t count) {
  if (edges.size() != edge_j.size()) {
    throw std::invalid_argument("edges and edge_j differ in size");
  }
  std::vector<std::size_t> nodes(lsh_label.size(), 0);
  for (const std::size_t label : lsh_label) {
    ++nodes[label];
  }
  std::vector<merge_example> merges;
  std::vector<std::size_t> slot(lsh_label.size(), lsh_label.size());  // lsh root -> position in merges
  for (const auto& p : parts_of(lsh_label, true_label)) {
    if (p.second >= 2) {
      slot[p.first] = merges.size();
      merges.push_back(merge_example{p.first, nodes[p.first], p.second, {0, 0}, 2.0});
    }
  }
  for (std::size_t e = 0; e < edges.size(); ++e) {
    const std::size_t s = slot[lsh_label[edges[e].first]];
    if (s == lsh_label.size()) {
      continue;
    }
    auto& m = merges[s];
    // the lowest J, ties by the smaller edge, so the result does not depend on the edge order
    if (edge_j[e] < m.weakest_j || (edge_j[e] == m.weakest_j && edges[e] < m.weakest)) {
      m.weakest_j = edge_j[e];
      m.weakest = edges[e];
    }
  }
  std::sort(merges.begin(), merges.end(), [](const merge_example& a, const merge_example& b) {
    if (a.true_parts != b.true_parts) {
      return a.true_parts > b.true_parts;
    }
    if (a.nodes != b.nodes) {
      return a.nodes > b.nodes;
    }
    return a.lsh_root < b.lsh_root;
  });
  merges.resize(std::min(count, merges.size()));
  return merges;
}

uint64_t input_fingerprint(const std::vector<std::size_t>& ids, const std::vector<std::vector<std::string>>& sets) {
  if (ids.size() != sets.size()) {
    throw std::invalid_argument("ids and sets differ in size");
  }
  XXHash64 hasher(0);
  std::string text;  // one add() per set: GCC's -Warray-bounds misreads add() of a short literal
  for (std::size_t s = 0; s < sets.size(); ++s) {
    text = std::to_string(ids[s]) + "\n";
    for (const auto& item : sets[s]) {
      text += item + "\n";
    }
    text += "\n";
    hasher.add(text.data(), text.size());
  }
  return hasher.hash();
}

std::string exact_pairs_header(const std::string& what, const std::string& archive_sha256, std::size_t shingle_words,
                               std::size_t indexed_posts, uint64_t input_xxh64) {
  std::array<char, 17> hex{};
  std::snprintf(hex.data(), hex.size(), "%016llx", static_cast<unsigned long long>(input_xxh64));  // NOLINT(google-runtime-int): the type of %llx
  return "# " + what + "\n" +
         "# archive_sha256=" + archive_sha256 + "\n" +
         "# shingle_words=" + std::to_string(shingle_words) + "\n" +
         "# indexed_posts=" + std::to_string(indexed_posts) + "\n" +
         "# input_xxh64=" + hex.data() + "\n";
}

void write_exact_pairs(std::ostream& out, const std::string& header, const std::vector<exact_pair>& pairs) {
  out << header << "# pairs=" << pairs.size() << '\n';
  for (const auto& p : pairs) {
    out << p.i << '\t' << p.j << '\t' << p.intersection << '\t' << p.union_size << '\n';
  }
}

bool read_exact_pairs(std::istream& in, const std::string& header, std::vector<exact_pair>& pairs) {
  pairs.clear();
  const auto header_lines = static_cast<std::size_t>(std::count(header.begin(), header.end(), '\n'));
  std::string seen;
  std::string line;
  for (std::size_t l = 0; l < header_lines && std::getline(in, line); ++l) {
    seen += line + "\n";
  }
  if (seen != header) {
    return false;
  }
  std::size_t expected = 0;
  {
    const std::string prefix = "# pairs=";
    if (!std::getline(in, line) || line.compare(0, prefix.size(), prefix) != 0) {
      throw std::runtime_error("the exact-pair cache has no \"# pairs=N\" line");
    }
    std::istringstream count(line.substr(prefix.size()));
    std::string rest;
    if (!(count >> expected) || (count >> rest)) {
      throw std::runtime_error("malformed pair count: " + line);
    }
  }
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    exact_pair p{};
    std::string rest;
    if (!(fields >> p.i >> p.j >> p.intersection >> p.union_size) || (fields >> rest) || p.i >= p.j ||
        p.intersection == 0 || p.intersection > p.union_size) {
      throw std::runtime_error("malformed exact-pair line: " + line);
    }
    pairs.push_back(p);
  }
  if (pairs.size() != expected) {
    throw std::runtime_error("the exact-pair cache has " + std::to_string(pairs.size()) + " pairs, but its header says " +
                             std::to_string(expected));
  }
  return true;
}

std::string decode_sgml_entities(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      const std::size_t end = text.find(';', i);
      const std::string name = end == std::string::npos ? std::string() : text.substr(i + 1, end - i - 1);
      int code = -1;
      if (name == "lt") {
        code = '<';
      } else if (name == "gt") {
        code = '>';
      } else if (name == "amp") {
        code = '&';
      } else if (name.size() >= 2 && name.size() <= 4 && name[0] == '#' &&
                 std::all_of(name.begin() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        code = std::stoi(name.substr(1));
        code = code < 256 ? code : -1;
      }
      if (code >= 0) {
        out += static_cast<char>(code);
        i = end + 1;
        continue;
      }
    }
    out += text[i];
    ++i;
  }
  return out;
}

namespace {

// The text between the first <tag> and the next </tag> in [begin, end), or "" if there is none.
std::string element_text(const std::string& text, std::size_t begin, std::size_t end, const std::string& tag) {
  const std::string open = "<" + tag + ">";
  const std::size_t start = text.find(open, begin);
  if (start == std::string::npos || start >= end) {
    return "";
  }
  const std::size_t stop = text.find("</" + tag + ">", start);
  if (stop == std::string::npos || stop > end) {
    return "";
  }
  return text.substr(start + open.size(), stop - start - open.size());
}

}  // namespace

std::vector<reuters_doc> parse_reuters_sgm(const std::string& text) {
  std::vector<reuters_doc> docs;
  const std::string open = "<REUTERS";
  const std::string close = "</REUTERS>";
  for (std::size_t at = text.find(open); at != std::string::npos; at = text.find(open, at)) {
    const std::size_t end = text.find(close, at);
    const std::size_t tag_end = text.find('>', at);
    if (end == std::string::npos || tag_end == std::string::npos || tag_end > end) {
      throw std::runtime_error("a <REUTERS> element has no </REUTERS>");
    }
    const std::string tag = text.substr(at, tag_end - at);
    const std::string attr = " NEWID=\"";
    const std::size_t id_at = tag.find(attr);
    const std::size_t id_end = id_at == std::string::npos ? id_at : tag.find('"', id_at + attr.size());
    if (id_end == std::string::npos) {
      throw std::runtime_error("a <REUTERS> element has no NEWID: " + tag);
    }
    docs.push_back(reuters_doc{tag.substr(id_at + attr.size(), id_end - id_at - attr.size()),
                               decode_sgml_entities(element_text(text, tag_end, end, "TITLE")) + "\n" +
                                   decode_sgml_entities(element_text(text, tag_end, end, "BODY"))});
    at = end + close.size();
  }
  return docs;
}

}  // namespace demo

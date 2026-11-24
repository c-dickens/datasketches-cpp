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

// The full-data check (MH-14b): the sketch and the LSH clusters against the exact Jaccard of every
// pair of indexed items, at every (k, r, t) of demo::full_grid(). lsh_20news_full and
// lsh_reuters_full give it their data (demo::full_source).
//
// The exact pairs are cached in <stem>_exact_pairs.tsv (with --smoke: <stem>_exact_pairs_smoke.tsv)
// next to the data directory. A cache with the right header is loaded; otherwise the program runs
// the exact pass and writes a new cache.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "minhash_lsh.hpp"
#include "news20/loader.hpp"

namespace demo {

namespace {

const char* const LICENCE_LINES[] = {
    "Licensed to the Apache Software Foundation (ASF) under one",
    "or more contributor license agreements.  See the NOTICE file",
    "distributed with this work for additional information",
    "regarding copyright ownership.  The ASF licenses this file",
    "to you under the Apache License, Version 2.0 (the",
    "\"License\"); you may not use this file except in compliance",
    "with the License.  You may obtain a copy of the License at",
    "",
    "  http://www.apache.org/licenses/LICENSE-2.0",
    "",
    "Unless required by applicable law or agreed to in writing,",
    "software distributed under the License is distributed on an",
    "\"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY",
    "KIND, either express or implied.  See the License for the",
    "specific language governing permissions and limitations",
    "under the License.",
};

constexpr std::size_t TOP_PAIRS = 20;
constexpr std::size_t SHARED_SHINGLES = 3;

namespace fs = std::filesystem;
using datasketches::minhash_lsh_index;
using datasketches::minhash_sketch;
using shingle_set = std::vector<std::string>;
using steady = std::chrono::steady_clock;

double seconds_since(steady::time_point start) {
  return std::chrono::duration<double>(steady::now() - start).count();
}

std::string fixed(double value, int digits) {
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%.*f", digits, value);
  return buf.data();
}

// A threshold: 1 decimal when that is exact (0.3), else 2 (0.45).
std::string tfix(double t) {
  return fixed(t, std::abs(t * 10.0 - std::round(t * 10.0)) < 1e-9 ? 1 : 2);
}

std::string general(double value) {
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%.3g", value);
  return buf.data();
}

std::string ratio(std::size_t num, std::size_t den) {
  return den == 0 ? std::string("NA") : fixed(static_cast<double>(num) / static_cast<double>(den), 4);
}

const char* verdict(bool pass) {
  return pass ? "PASS" : "FAIL";
}

std::size_t common_count(const shingle_set& a, const shingle_set& b) {
  std::size_t common = 0;
  auto ia = a.begin();
  auto ib = b.begin();
  while (ia != a.end() && ib != b.end()) {
    if (*ia < *ib) {
      ++ia;
    } else if (*ib < *ia) {
      ++ib;
    } else {
      ++common;
      ++ia;
      ++ib;
    }
  }
  return common;
}

std::string shared_shingles(const shingle_set& a, const shingle_set& b) {
  std::vector<std::string> common;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
  std::string out;
  for (std::size_t s = 0; s < common.size() && s < SHARED_SHINGLES; ++s) {
    out += (s == 0 ? "`" : ", `") + common[s] + "`";
  }
  return out;
}

struct grid_result {
  demo::grid_point point{};
  std::size_t min_matches = 0;
  std::size_t candidates = 0;
  demo::pair_check pairs{};
  demo::cluster_metrics clusters{};
  std::vector<demo::merge_example> worst;
  std::size_t external_split = 0;  // external pairs not in one true cluster, or not in one LSH cluster
  std::array<demo::band_rate, demo::NUM_BINS> bands{};  // the verified pairs of each J band
  demo::kept_counts kept{};  // deduplication by the LSH clusters against the true clusters
  std::size_t verified_proxy = 0;     // verified pairs in one proxy group
  demo::cluster_metrics proxy_lsh{};  // the LSH clusters against the proxy groups
};

struct estimate_row {
  uint16_t k = 0;
  std::size_t pairs = 0;
  double mean_error = 0.0;
  double rmse = 0.0;
  double rms_sd = 0.0;  // RMS of sqrt(J (1 - J) / k)
};

double recall_of(const demo::pair_check& c) {
  return c.counts.similar == 0 ? 0.0 : static_cast<double>(c.counts.both) / static_cast<double>(c.counts.similar);
}

double precision_of(const demo::pair_check& c) {
  return c.counts.verified == 0 ? 0.0 : static_cast<double>(c.counts.both) / static_cast<double>(c.counts.verified);
}

bool meets_target(const grid_result& g, bool require_pass) {
  return (g.pairs.pass || !require_pass) && g.pairs.counts.similar > 0 && g.pairs.counts.verified > 0 &&
         recall_of(g.pairs) >= demo::FULL_TARGET && precision_of(g.pairs) >= demo::FULL_TARGET;
}

std::string tsv_licence() {
  std::string out;
  for (const char* line : LICENCE_LINES) {
    out += std::string(line).empty() ? "#\n" : "# " + std::string(line) + "\n";
  }
  return out;
}

void write_file(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

constexpr double LOW_BIN_WIDTH = 0.001;  // the J bins of the pairs below the floor

// The pairs with 0 < J < floor, by J bin of width LOW_BIN_WIDTH.
struct low_bins {
  std::vector<std::size_t> all;
  std::vector<std::size_t> cross;  // of them, the cross pairs of the held-out split
};

// shortcut: the middle of its bin stands for each pair below the floor; the bins are 0.001 wide, so the
// error is far below the spread of the counts. Store the pairs if a later floor run needs them exactly.
double low_bin_j(std::size_t b) {
  return (static_cast<double>(b) + 0.5) * LOW_BIN_WIDTH;
}

void write_low_bins(const fs::path& path, const std::string& header, const low_bins& bins) {
  std::ostringstream out;
  out << header << "# bins=" << bins.all.size() << "\n";
  for (std::size_t b = 0; b < bins.all.size(); ++b) {
    out << b << "\t" << bins.all[b] << "\t" << bins.cross[b] << "\n";
  }
  const fs::path part = path.string() + ".part";
  write_file(part, out.str());
  fs::rename(part, path);
}

// Returns false if the file is missing or has another header or bin count.
bool read_low_bins(const fs::path& path, const std::string& header, std::size_t n_bins, low_bins& bins) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string head = header + "# bins=" + std::to_string(n_bins) + "\n";
  if (text.compare(0, head.size(), head) != 0) {
    return false;
  }
  std::istringstream rows(text.substr(head.size()));
  bins.all.assign(n_bins, 0);
  bins.cross.assign(n_bins, 0);
  for (std::size_t b = 0; b < n_bins; ++b) {
    std::size_t index = 0;
    if (!(rows >> index >> bins.all[b] >> bins.cross[b]) || index != b) {
      throw std::runtime_error("malformed line in " + path.string());
    }
  }
  return true;
}

// shortcut: the two-sided normal approximation, with a continuity correction, of a count with this mean and
// variance. The exact Poisson-binomial program is O(n * count), too slow for the pairs below a floor.
double normal_p_value(double observed, double mean, double variance) {
  if (!(variance > 0.0)) {
    return std::abs(observed - mean) < 0.5 ? 1.0 : 0.0;
  }
  const double z = std::max(0.0, std::abs(observed - mean) - 0.5) / std::sqrt(variance);
  return std::min(1.0, std::erfc(z / std::sqrt(2.0)));
}

// Adds the pairs below the floor (their count, mean and variance of the verified count) to the false side of
// a pair check, and recomputes its precision p-value with normal_p_value.
void add_low_pairs(demo::pair_check& c, const std::vector<double>& exact, const std::vector<double>& prob, double t,
                   std::size_t low_count, double low_mean, double low_variance, double test_alpha) {
  double variance = low_variance;
  for (std::size_t i = 0; i < exact.size(); ++i) {
    if (exact[i] > 0.0 && exact[i] < t) {
      variance += prob[i] * (1.0 - prob[i]);
    }
  }
  c.false_pairs += low_count;
  c.expected_false += low_mean;
  c.p_precision = normal_p_value(static_cast<double>(c.counts.verified - c.counts.both), c.expected_false, variance);
  c.pass = c.p_recall >= test_alpha && c.p_precision >= test_alpha;
}

struct heldout_row {
  std::string setting;
  const grid_result* in_sample = nullptr;
  demo::pair_check cross{};
  std::size_t queries_with_match = 0;  // queries with at least 1 true cross pair
  std::size_t queries_hit = 0;         // of them, the queries with at least 1 verified true cross pair
};

// The held-out check: index only the items with in_a[u], and query with each other item through
// query_candidates(). Only the cross pairs (one item in each set) count. Report only.
std::vector<heldout_row> run_heldout(const std::vector<bool>& in_a, const std::vector<shingle_set>& sets,
                                     const demo::edge_list& pair_nodes, const std::vector<double>& exact_j,
                                     const std::unordered_map<uint64_t, std::size_t>& pair_index,
                                     const std::vector<std::pair<std::string, const grid_result*>>& settings,
                                     double test_alpha, const low_bins& low) {
  const std::size_t n = in_a.size();
  std::vector<std::size_t> index_ids;
  std::vector<std::size_t> query_ids;
  for (std::size_t u = 0; u < n; ++u) {
    (in_a[u] ? index_ids : query_ids).push_back(u);
  }
  std::vector<std::size_t> cross;  // the stored pairs with one item in each set
  for (std::size_t e = 0; e < pair_nodes.size(); ++e) {
    if (in_a[pair_nodes[e].first] != in_a[pair_nodes[e].second]) {
      cross.push_back(e);
    }
  }
  std::vector<heldout_row> heldout;
  std::map<uint16_t, std::vector<minhash_sketch>> sketches_at;  // built once for each k
  for (const auto& setting : settings) {
    const demo::grid_point& gp = setting.second->point;
    auto& sk = sketches_at[gp.k];
    if (sk.empty()) {
      for (std::size_t u = 0; u < n; ++u) {
        sk.push_back(demo::sketch_of(sets[u], gp.k));
      }
    }
    const auto bands = static_cast<uint16_t>(gp.bands);
    minhash_lsh_index index(bands, gp.threshold);
    std::vector<std::vector<uint64_t>> index_keys;
    for (const std::size_t u : index_ids) {
      index.add(u, sk[u]);
      const auto keys = minhash_lsh_index::band_keys(sk[u], bands);
      index_keys.emplace_back(keys.begin(), keys.end());
    }
    const demo::bucket_table table(index_keys, index_ids);
    std::set<std::pair<std::size_t, std::size_t>> verified;  // (smaller, larger)
    for (const std::size_t q : query_ids) {
      const auto found = index.query_candidates(sk[q]);
      const auto keys = minhash_lsh_index::band_keys(sk[q], bands);
      if (std::vector<std::size_t>(found.begin(), found.end()) !=
          table.candidates(std::vector<uint64_t>(keys.begin(), keys.end()))) {
        throw std::runtime_error("query_candidates() differs from the band-key bucket rule at k = " +
                                 std::to_string(gp.k) + ", r = " + std::to_string(gp.rows));
      }
      for (const uint64_t a : found) {
        if (sk[q].jaccard(sk[a]) >= gp.threshold) {
          verified.insert(std::minmax(q, static_cast<std::size_t>(a)));
        }
      }
    }
    const auto q_table = demo::full_band_given_matches(gp.rows, gp.bands);
    const std::size_t m = demo::min_matches(gp.threshold, gp.k);
    std::vector<double> cross_j;
    std::vector<double> cross_prob;
    std::vector<bool> cross_verified;
    std::vector<bool> has_match(n, false);
    std::vector<bool> hit(n, false);
    for (const std::size_t e : cross) {
      const double j = exact_j[e];
      const bool v = verified.count(pair_nodes[e]) > 0;
      cross_j.push_back(j);
      cross_prob.push_back(demo::reported_tail(demo::binomial_pmf(j, gp.k), q_table)[m]);
      cross_verified.push_back(v);
      if (j >= gp.threshold) {
        const std::size_t query = in_a[pair_nodes[e].first] ? pair_nodes[e].second : pair_nodes[e].first;
        has_match[query] = true;
        hit[query] = hit[query] || v;
      }
    }
    std::size_t disjoint = 0;  // verified cross pairs with J = 0
    for (const auto& pr : verified) {
      disjoint += pair_index.count(static_cast<uint64_t>(pr.first) * n + pr.second) == 0 ? 1 : 0;
    }
    heldout_row row;
    row.setting = setting.first;
    row.in_sample = setting.second;
    // report only: the pass flag is not used
    row.cross = demo::check_pair_tests(cross_j, cross_prob, cross_verified, disjoint, gp.threshold, test_alpha);
    if (!low.cross.empty()) {  // the verified cross pairs below the floor are in `disjoint`
      std::size_t low_count = 0;
      double low_mean = 0.0;
      double low_variance = 0.0;
      for (std::size_t b = 0; b < low.cross.size(); ++b) {
        const double p = demo::reported_tail(demo::binomial_pmf(low_bin_j(b), gp.k), q_table)[m];
        const auto count = static_cast<double>(low.cross[b]);
        low_count += low.cross[b];
        low_mean += count * p;
        low_variance += count * p * (1.0 - p);
      }
      add_low_pairs(row.cross, cross_j, cross_prob, gp.threshold, low_count, low_mean, low_variance, test_alpha);
    }
    for (const std::size_t q : query_ids) {
      row.queries_with_match += has_match[q] ? 1 : 0;
      row.queries_hit += hit[q] ? 1 : 0;
    }
    heldout.push_back(row);
  }
  return heldout;
}

// Adds the pairs below the floor to the J bands and to the false side of the pair check of one grid point.
// lp is P(verified | J) of each low bin, and low_verified counts the verified pairs below the floor by J band.
void add_low_bins(grid_result& g, const low_bins& low, const std::vector<double>& lp,
                  const std::array<std::size_t, demo::NUM_BINS>& low_verified, std::size_t low_count,
                  const std::vector<double>& exact_j, const std::vector<double>& prob, double t, double test_alpha) {
  double low_mean = 0.0;
  double low_variance = 0.0;
  for (std::size_t b = 0; b < low.all.size(); ++b) {
    const auto count = static_cast<double>(low.all[b]);
    low_mean += count * lp[b];
    low_variance += count * lp[b] * (1.0 - lp[b]);
    auto& band = g.bands[demo::jaccard_bin(low_bin_j(b))];
    band.pairs += low.all[b];
    band.expected += count * lp[b];
  }
  for (std::size_t band = 0; band < demo::NUM_BINS; ++band) {
    g.bands[band].verified += low_verified[band];
  }
  add_low_pairs(g.pairs, exact_j, prob, t, low_count, low_mean, low_variance, test_alpha);
}

// The true pairs in one proxy group, and the true clusters against the proxy groups, at each threshold.
void proxy_truth(const full_source& src, const demo::edge_list& pair_nodes, const std::vector<double>& exact_j,
                 const std::vector<std::vector<std::size_t>>& true_label, std::vector<std::size_t>& true_proxy_pairs,
                 std::vector<demo::cluster_metrics>& proxy_true) {
  for (std::size_t ti = 0; ti < src.thresholds.size(); ++ti) {
    for (std::size_t e = 0; e < pair_nodes.size(); ++e) {
      const auto& pr = pair_nodes[e];
      true_proxy_pairs[ti] +=
          exact_j[e] >= src.thresholds[ti] && src.proxy_group[pr.first] == src.proxy_group[pr.second] ? 1 : 0;
    }
    proxy_true[ti] = demo::compare_clusters(true_label[ti], src.proxy_group);
  }
}

// The report-only proxy section of the report (Markdown), and its TSV in proxy_tsv.
std::string proxy_section(const full_source& src, const std::vector<grid_result>& results, std::size_t n_t,
                          const std::vector<std::size_t>& true_proxy_pairs,
                          const std::vector<demo::cluster_metrics>& proxy_true, const std::string& meta,
                          unsigned ref_bands, std::ostringstream& proxy_tsv) {
  std::ostringstream md;
  const std::size_t proxy_pairs = proxy_true[0].true_pairs;
  proxy_tsv << tsv_licence() << meta << "# proxy_pairs=" << proxy_pairs << "\n"
            << "K\tR\tB\tT\tTruePairs\tTrueProxy\tVerifiedPairs\tVerifiedProxy\tTrueCoPairs\tTrueCoProxy\t"
               "LshCoPairs\tLshCoProxy\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& g = results[i];
    const std::size_t ti = i % n_t;
    proxy_tsv << g.point.k << "\t" << g.point.rows << "\t" << g.point.bands << "\t" << tfix(g.point.threshold) << "\t"
              << g.pairs.counts.similar << "\t" << true_proxy_pairs[ti] << "\t" << g.pairs.counts.verified << "\t"
              << g.verified_proxy << "\t" << proxy_true[ti].lsh_pairs << "\t" << proxy_true[ti].both_pairs << "\t"
              << g.proxy_lsh.lsh_pairs << "\t" << g.proxy_lsh.both_pairs << "\n";
  }
  md << "## " << src.proxy_title << " (report only)\n\n";
  md << src.proxy_text << "\n\n";
  md << "- proxy pairs (two " << src.nouns << " of one group): " << proxy_pairs << "\n\n";
  md << "Each cell is precision / recall against the proxy: the share of the pairs that are proxy pairs, and the "
        "share of the proxy pairs that they cover. True pairs and true clusters use the exact J, so they show the "
        "gap between the item and the proxy. Verified pairs and LSH clusters add the error of the sketch. At k = "
     << src.ref_k << ", " << ref_bands << " × " << src.ref_rows << "; all grid points are in `" << src.stem
     << "_proxy.tsv`.\n\n";
  md << "| t | true pairs | verified pairs | true clusters | LSH clusters |\n|---:|---|---|---|---|\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& g = results[i];
    if (g.point.k != src.ref_k || g.point.rows != src.ref_rows) {
      continue;
    }
    const std::size_t ti = i % n_t;
    const auto cell = [proxy_pairs](std::size_t in_proxy, std::size_t total) {
      return ratio(in_proxy, total) + " / " + ratio(in_proxy, proxy_pairs);
    };
    md << "| " << tfix(g.point.threshold) << " | " << cell(true_proxy_pairs[ti], g.pairs.counts.similar) << " | "
       << cell(g.verified_proxy, g.pairs.counts.verified) << " | "
       << cell(proxy_true[ti].both_pairs, proxy_true[ti].lsh_pairs) << " | "
       << cell(g.proxy_lsh.both_pairs, g.proxy_lsh.lsh_pairs) << " |\n";
  }
  md << "\n";
  return md.str();
}

struct exact_result {
  std::vector<demo::exact_pair> pairs;  // the pairs with J >= floor (J > 0 without a floor)
  low_bins low;                         // the pairs with 0 < J < floor, by bin
  bool loaded = false;                  // read from the cache
  fs::path cache;                       // the cache file
};

// The exact pass over every pair of indexed items, or its cache next to the data directory.
exact_result exact_pass(const full_source& src, bool smoke, const fs::path& data_dir, const std::vector<bool>& in_a) {
  const auto& post_of = src.item_of;
  const auto& sets = src.sets;
  const std::size_t n = post_of.size();
  const fs::path cache = data_dir.parent_path() / (src.stem + (smoke ? "_exact_pairs_smoke.tsv" : "_exact_pairs.tsv"));
  const std::string header = demo::exact_pairs_header(
      src.stem + " exact pairs (" + src.ticket + "): i, j, |A n B|, |A u B| of each pair of indexed " + src.nouns +
          (src.floor > 0.0 ? " with J >= " + tfix(src.floor) : std::string(" with J > 0")),
      src.archive_sha256, src.shingle_words, n, demo::input_fingerprint(post_of, sets));
  const auto exact_start = steady::now();
  exact_result r;
  r.cache = cache;
  auto& exact = r.pairs;
  auto& low = r.low;
  auto& loaded = r.loaded;
  const fs::path low_cache =
      data_dir.parent_path() / (src.stem + (smoke ? "_exact_pairs_low_smoke.tsv" : "_exact_pairs_low.tsv"));
  const auto n_low_bins = static_cast<std::size_t>(std::ceil(src.floor / LOW_BIN_WIDTH - 1e-9));
  {
    std::ifstream in(cache, std::ios::binary);
    loaded = in && demo::read_exact_pairs(in, header, exact) &&
             (src.floor == 0.0 || read_low_bins(low_cache, header, n_low_bins, low));
  }
  if (loaded) {
    std::cout << "exact pairs: loaded " << exact.size() << " from " << cache.string() << "\n";
  } else {
    std::cout << "exact pairs: no cache with this header, running the exact pass\n";
    exact.clear();
    low.all.assign(n_low_bins, 0);
    low.cross.assign(n_low_bins, 0);
    for (std::size_t u = 0; u < n; ++u) {
      for (std::size_t v = u + 1; v < n; ++v) {
        const double j = news20::jaccard(sets[u], sets[v]);
        if (j > 0.0 && j < src.floor) {
          const std::size_t b = std::min(n_low_bins - 1, static_cast<std::size_t>(j / LOW_BIN_WIDTH));
          ++low.all[b];
          low.cross[b] += in_a[u] != in_a[v] ? 1 : 0;
        } else if (j > 0.0) {
          const std::size_t common = common_count(sets[u], sets[v]);
          const std::size_t united = sets[u].size() + sets[v].size() - common;
          if (static_cast<double>(common) / static_cast<double>(united) != j) {
            throw std::runtime_error("the intersection count does not give news20::jaccard");
          }
          exact.push_back(demo::exact_pair{post_of[u], post_of[v], common, united});
          if (exact.size() > demo::MAX_EXACT_PAIRS) {
            throw std::runtime_error("more than " + std::to_string(demo::MAX_EXACT_PAIRS) + " pairs with J > 0");
          }
        }
      }
      if (u % 1000 == 999) {
        std::cerr << "  exact pass: " << u + 1 << " of " << n << " " << src.nouns << ", " << exact.size() << " pairs, "
                  << fixed(seconds_since(exact_start), 0) << " s\n";
      }
    }
    const fs::path part = cache.string() + ".part";
    {
      std::ofstream out(part, std::ios::binary);
      demo::write_exact_pairs(out, header, exact);
      if (!out) {
        throw std::runtime_error("cannot write " + part.string());
      }
    }
    fs::rename(part, cache);
    std::cout << "exact pairs: computed " << exact.size() << ", wrote " << cache.string() << "\n";
    if (src.floor > 0.0) {
      write_low_bins(low_cache, header, low);
    }
  }
  return r;
}

int run(const full_source& src, bool smoke, const fs::path& data_dir, const fs::path& out_dir,
        steady::time_point start) {
  fs::create_directories(out_dir);
  const auto& post_of = src.item_of;
  const auto& sets = src.sets;
  const std::size_t n = post_of.size();
  if (sets.size() != n || src.names.size() != n) {
    throw std::invalid_argument("item_of, sets and names differ in size");
  }
  std::vector<std::size_t> node_of(src.loaded, n);
  for (std::size_t u = 0; u < n; ++u) {
    if (sets[u].empty() || post_of[u] >= src.loaded) {
      throw std::invalid_argument("node " + std::to_string(u) + " has no shingle or no item");
    }
    node_of[post_of[u]] = u;
  }
  const auto post_name = [&src](std::size_t u) { return src.names[u]; };
  const auto name = [&post_name](std::size_t u) { return "`" + post_name(u) + "`"; };
  std::cout << src.nouns << " " << src.chosen << ", indexed " << n << " (" << fixed(seconds_since(start), 1) << " s)\n";

  // The held-out split: the index set and the query set.
  std::vector<bool> in_a(n);
  std::size_t index_size = 0;
  for (std::size_t u = 0; u < n; ++u) {
    in_a[u] = demo::in_index_set(post_of[u], demo::HELDOUT_SEED);
    index_size += in_a[u] ? 1 : 0;
  }
  if (src.floor > 0.0 && src.floor > src.thresholds.front()) {
    throw std::invalid_argument("the floor is above the smallest threshold");
  }

  // The exact pass, or its cache.
  const auto exact_start = steady::now();
  const exact_result exact_out = exact_pass(src, smoke, data_dir, in_a);
  const auto& exact = exact_out.pairs;
  const low_bins& low = exact_out.low;
  const bool loaded = exact_out.loaded;
  const fs::path& cache = exact_out.cache;
  const auto n_low_bins = low.all.size();
  std::size_t low_count = 0;
  for (const std::size_t c : low.all) {
    low_count += c;
  }
  const double exact_seconds = seconds_since(exact_start);
  std::cout << "exact pass or cache: " << fixed(exact_seconds, 1) << " s\n";

  // Pair e as nodes, with its exact J.
  const std::size_t n_pairs = exact.size();
  demo::edge_list pair_nodes;
  std::vector<double> exact_j;
  pair_nodes.reserve(n_pairs);
  exact_j.reserve(n_pairs);
  std::unordered_map<uint64_t, std::size_t> pair_index;  // u * n + v -> e; only looked up
  pair_index.reserve(n_pairs);
  for (const auto& p : exact) {
    const std::size_t u = p.i < node_of.size() ? node_of[p.i] : n;
    const std::size_t v = p.j < node_of.size() ? node_of[p.j] : n;
    if (u >= n || v >= n) {
      throw std::runtime_error("the cache has a " + src.noun + " that is not indexed");
    }
    const double j = static_cast<double>(p.intersection) / static_cast<double>(p.union_size);
    // A cross-check of the input fingerprint in the header: each loaded pair must give news20::jaccard.
    if (loaded && news20::jaccard(sets[u], sets[v]) != j) {
      throw std::runtime_error("the cache does not match news20::jaccard; delete " + cache.string() +
                               " to run the exact pass again");
    }
    pair_index.emplace(static_cast<uint64_t>(u) * n + v, pair_nodes.size());
    pair_nodes.emplace_back(u, v);
    exact_j.push_back(j);
  }

  for (const auto& pr : src.external) {
    if (pr.first >= n || pr.second >= n) {
      throw std::invalid_argument("an external pair has a node >= n");
    }
  }

  // The true clusters at each t.
  std::vector<std::vector<std::size_t>> true_label;
  for (const double t : src.thresholds) {
    demo::edge_list edges;
    for (std::size_t e = 0; e < n_pairs; ++e) {
      if (exact_j[e] >= t) {
        edges.push_back(pair_nodes[e]);
      }
    }
    true_label.push_back(demo::components(n, edges));
  }
  const bool has_proxy = !src.proxy_group.empty();
  if (has_proxy && src.proxy_group.size() != n) {
    throw std::invalid_argument("proxy_group has the wrong size");
  }
  std::vector<std::size_t> true_proxy_pairs(src.thresholds.size(), 0);  // true pairs in one proxy group
  std::vector<demo::cluster_metrics> proxy_true(src.thresholds.size());  // the true clusters against the groups
  if (has_proxy) {
    proxy_truth(src, pair_nodes, exact_j, true_label, true_proxy_pairs, proxy_true);
  }

  std::vector<demo::grid_point> grid;  // as demo::full_grid(), with the rows and thresholds of src
  for (const uint16_t k : demo::FULL_K_VALUES) {
    for (const unsigned rows : src.rows) {
      for (const double t : src.thresholds) {
        grid.push_back(demo::grid_point{k, rows, k / rows, t});
      }
    }
  }
  const double test_alpha = demo::FULL_ALPHA / static_cast<double>(2 * grid.size());
  const std::size_t n_rows = src.rows.size();
  const std::size_t n_t = src.thresholds.size();
  std::vector<grid_result> results(grid.size());
  // The reported pairs of the reference setting [0] and of compare_k, compare_rows [1], at t = ref_threshold.
  struct reported_pairs {
    demo::edge_list edges;
    std::vector<double> exact;
    std::vector<double> estimate;
    std::vector<std::size_t> lsh_label;
  };
  std::array<reported_pairs, 2> saved{};
  std::vector<estimate_row> estimates(demo::FULL_K_VALUES.size());
  std::vector<std::vector<uint64_t>> max_signature(n);
  std::size_t prefix_checked = 0;
  std::size_t prefix_failed = 0;
  double tail_seconds = 0.0;

  // The largest k first: its signatures check the prefix of every smaller k.
  for (std::size_t ki = demo::FULL_K_VALUES.size(); ki > 0; --ki) {
    const uint16_t k = demo::FULL_K_VALUES[ki - 1];
    const auto k_start = steady::now();
    std::vector<minhash_sketch> sketches;
    sketches.reserve(n);
    for (std::size_t u = 0; u < n; ++u) {
      sketches.push_back(demo::sketch_of(sets[u], k));
      const auto& sig = sketches[u].get_signature();
      if (ki == demo::FULL_K_VALUES.size()) {
        max_signature[u].assign(sig.begin(), sig.end());
      } else {
        ++prefix_checked;
        if (!std::equal(sig.begin(), sig.end(), max_signature[u].begin())) {
          ++prefix_failed;
        }
      }
    }

    // The estimate error on the pairs with exact J >= ESTIMATE_MIN_J.
    estimate_row& est = estimates[ki - 1];
    est.k = k;
    double error_sum = 0.0;
    double square_sum = 0.0;
    double variance_sum = 0.0;
    for (std::size_t e = 0; e < n_pairs; ++e) {
      const double j = exact_j[e];
      if (j >= demo::ESTIMATE_MIN_J) {
        const double error = sketches[pair_nodes[e].first].jaccard(sketches[pair_nodes[e].second]) - j;
        ++est.pairs;
        error_sum += error;
        square_sum += error * error;
        variance_sum += j * (1.0 - j) / k;
      }
    }
    if (est.pairs > 0) {
      const auto count = static_cast<double>(est.pairs);
      est.mean_error = error_sum / count;
      est.rmse = std::sqrt(square_sum / count);
      est.rms_sd = std::sqrt(variance_sum / count);
    }

    // P(reported | J) of every pair, at each (r, t) of this k.
    std::vector<std::vector<double>> q;
    for (const unsigned rows : src.rows) {
      q.push_back(demo::full_band_given_matches(rows, k / rows));
    }
    std::vector<std::size_t> m;
    for (const double t : src.thresholds) {
      m.push_back(demo::min_matches(t, k));
    }
    std::vector<std::vector<double>> prob(n_rows * n_t, std::vector<double>(n_pairs));
    std::vector<std::vector<double>> low_prob(n_rows * n_t, std::vector<double>(n_low_bins));  // of each low bin
    for (std::size_t b = 0; b < n_low_bins; ++b) {
      const auto pmf = demo::binomial_pmf(low_bin_j(b), k);
      for (std::size_t ri = 0; ri < n_rows; ++ri) {
        const auto tail = demo::reported_tail(pmf, q[ri]);
        for (std::size_t ti = 0; ti < n_t; ++ti) {
          low_prob[ri * n_t + ti][b] = tail[m[ti]];
        }
      }
    }
    for (std::size_t e = 0; e < n_pairs; ++e) {
      const auto pmf = demo::binomial_pmf(exact_j[e], k);
      for (std::size_t ri = 0; ri < n_rows; ++ri) {
        const auto tail = demo::reported_tail(pmf, q[ri]);
        for (std::size_t ti = 0; ti < n_t; ++ti) {
          prob[ri * n_t + ti][e] = tail[m[ti]];
        }
      }
    }

    for (std::size_t ri = 0; ri < n_rows; ++ri) {
      const unsigned rows = src.rows[ri];
      const auto bands = static_cast<uint16_t>(k / rows);
      std::vector<std::vector<uint64_t>> keys;
      keys.reserve(n);
      for (const auto& s : sketches) {
        const auto band_keys = minhash_lsh_index::band_keys(s, bands);
        keys.emplace_back(band_keys.begin(), band_keys.end());
      }
      // The candidates: the members of each bucket, then sorted.
      std::unordered_set<uint64_t> candidate_set;  // sorted below, before any use
      std::vector<std::pair<uint64_t, std::size_t>> column(n);
      for (std::size_t band = 0; band < bands; ++band) {
        for (std::size_t u = 0; u < n; ++u) {
          column[u] = {keys[u][band], u};
        }
        std::sort(column.begin(), column.end());
        for (std::size_t a = 0; a < n;) {
          std::size_t end = a + 1;
          while (end < n && column[end].first == column[a].first) {
            ++end;
          }
          for (std::size_t x = a; x < end; ++x) {
            for (std::size_t y = x + 1; y < end; ++y) {
              candidate_set.insert(static_cast<uint64_t>(column[x].second) * n + column[y].second);
            }
          }
          a = end;
        }
      }
      std::vector<uint64_t> candidates(candidate_set.begin(), candidate_set.end());
      std::sort(candidates.begin(), candidates.end());
      std::vector<double> candidate_estimate;
      candidate_estimate.reserve(candidates.size());
      for (const uint64_t c : candidates) {
        candidate_estimate.push_back(sketches[c / n].jaccard(sketches[c % n]));
      }

      for (std::size_t ti = 0; ti < n_t; ++ti) {
        const double t = src.thresholds[ti];
        grid_result& g = results[((ki - 1) * n_rows + ri) * n_t + ti];
        g.point = demo::grid_point{k, rows, bands, t};
        g.min_matches = m[ti];
        g.candidates = candidates.size();
        // The reported pairs: candidates with Ĵ >= t.
        std::vector<bool> reported(n_pairs, false);
        std::size_t reported_disjoint = 0;  // verified pairs that are not stored: J = 0, or J below the floor
        std::array<std::size_t, demo::NUM_BINS> low_verified{};  // of them, the pairs with J > 0, by J band
        demo::edge_list edges;
        std::vector<double> edge_j;
        std::vector<double> edge_estimate;
        for (std::size_t c = 0; c < candidates.size(); ++c) {
          if (candidate_estimate[c] < t) {
            continue;
          }
          const std::size_t u = candidates[c] / n;
          const std::size_t v = candidates[c] % n;
          const auto it = pair_index.find(candidates[c]);
          if (it == pair_index.end()) {
            ++reported_disjoint;
            const double j = src.floor > 0.0 ? news20::jaccard(sets[u], sets[v]) : 0.0;
            if (j > 0.0) {
              ++low_verified[demo::jaccard_bin(j)];
            }
            edge_j.push_back(j);
          } else {
            reported[it->second] = true;
            edge_j.push_back(exact_j[it->second]);
          }
          edges.emplace_back(u, v);
          edge_estimate.push_back(candidate_estimate[c]);
        }
        const auto tail_start = steady::now();
        g.pairs = demo::check_pair_tests(exact_j, prob[ri * n_t + ti], reported, reported_disjoint, t, test_alpha);
        g.bands = demo::band_rates(exact_j, prob[ri * n_t + ti], reported);
        if (src.floor > 0.0) {
          add_low_bins(g, low, low_prob[ri * n_t + ti], low_verified, low_count, exact_j, prob[ri * n_t + ti], t,
                       test_alpha);
        }
        tail_seconds += seconds_since(tail_start);

        const auto lsh_label = demo::components(n, edges);
        if (t == src.ref_threshold && ((k == src.ref_k && rows == src.ref_rows) ||
                                       (k == src.compare_k && rows == src.compare_rows))) {
          saved[k == src.ref_k && rows == src.ref_rows ? 0 : 1] = reported_pairs{edges, edge_j, edge_estimate, lsh_label};
        }
        // Consistency: the components of the reported pairs are the clusters of the index.
        minhash_lsh_index index(bands, t);
        for (std::size_t u = 0; u < n; ++u) {
          index.add(u, sketches[u]);
        }
        std::vector<std::vector<std::size_t>> index_clusters;
        for (const auto& cl : index.get_duplicate_clusters()) {
          index_clusters.emplace_back(cl.begin(), cl.end());
        }
        if (index_clusters != demo::clusters_of(lsh_label)) {
          throw std::runtime_error("the components of the reported pairs differ from get_duplicate_clusters() at k = " +
                                   std::to_string(k) + ", r = " + std::to_string(rows) + ", t = " + tfix(t));
        }
        g.clusters = demo::compare_clusters(lsh_label, true_label[ti]);
        g.worst = demo::worst_merges(lsh_label, true_label[ti], edges, edge_j, demo::WORST_MERGES);
        g.kept = demo::compare_kept(lsh_label, true_label[ti]);
        if (has_proxy) {
          for (const auto& edge : edges) {
            g.verified_proxy += src.proxy_group[edge.first] == src.proxy_group[edge.second] ? 1 : 0;
          }
          g.proxy_lsh = demo::compare_clusters(lsh_label, src.proxy_group);
        }
        for (const auto& pr : src.external) {
          if (true_label[ti][pr.first] != true_label[ti][pr.second] || lsh_label[pr.first] != lsh_label[pr.second]) {
            ++g.external_split;
          }
        }
      }
    }
    std::cout << "k = " << k << ": " << fixed(seconds_since(k_start), 1) << " s\n";
  }
  std::cout << "tails: " << fixed(tail_seconds, 3) << " s\n";

  // Verdicts.
  std::size_t pair_failed = 0;
  std::size_t external_failed = 0;
  for (const auto& g : results) {
    pair_failed += g.pairs.pass ? 0 : 1;
    external_failed += g.external_split;
  }
  const bool prefix_pass = prefix_failed == 0;
  const bool external_pass = external_failed == 0;
  const bool overall = (pair_failed == 0 || !src.pair_test_in_overall) && prefix_pass && external_pass;

  // The reference setting: k = ref_k, (ref_k / ref_rows) x ref_rows, t = ref_threshold.
  const grid_result* ref = nullptr;
  for (const auto& g : results) {
    if (g.point.k == src.ref_k && g.point.rows == src.ref_rows && g.point.threshold == src.ref_threshold) {
      ref = &g;
    }
  }
  const auto t_ref = static_cast<std::size_t>(
      std::find(src.thresholds.begin(), src.thresholds.end(), src.ref_threshold) - src.thresholds.begin());
  const unsigned ref_bands = src.ref_k / src.ref_rows;
  const auto band_label = [](std::size_t band) {
    return "[" + fixed(static_cast<double>(band) / demo::NUM_BINS, 1) + ", " +
           fixed(static_cast<double>(band + 1) / demo::NUM_BINS, 1) + (band + 1 == demo::NUM_BINS ? "]" : ")");
  };

  // TSV files.
  std::ostringstream meta;
  meta << "# ticket=" << src.ticket << "\n# smoke=" << (smoke ? 1 : 0) << "\n# indexed_posts=" << n << "\n# pairs_j_gt_0=" << n_pairs
       << "\n# family_alpha=" << general(demo::FULL_ALPHA) << "\n# test_alpha=" << general(test_alpha) << "\n";
  if (src.floor > 0.0) {
    meta << "# floor=" << tfix(src.floor) << "\n# pairs_below_floor=" << low_count << "\n";
  }
  std::ostringstream bands_tsv;
  bands_tsv << tsv_licence() << meta.str()
            << "K\tR\tB\tT\tBandLo\tBandHi\tPairs\tVerified\tVerifiedRate\tExpectedVerified\tExpectedRate\n";
  for (const auto& g : results) {
    for (std::size_t band = 0; band < demo::NUM_BINS; ++band) {
      const auto& b = g.bands[band];
      const auto count = static_cast<double>(b.pairs);
      bands_tsv << g.point.k << "\t" << g.point.rows << "\t" << g.point.bands << "\t" << tfix(g.point.threshold)
                << "\t" << fixed(static_cast<double>(band) / demo::NUM_BINS, 1) << "\t"
                << fixed(static_cast<double>(band + 1) / demo::NUM_BINS, 1) << "\t" << b.pairs << "\t" << b.verified
                << "\t" << (b.pairs == 0 ? "NA" : fixed(static_cast<double>(b.verified) / count, 6)) << "\t"
                << fixed(b.expected, 4) << "\t" << (b.pairs == 0 ? "NA" : fixed(b.expected / count, 6)) << "\n";
    }
  }

  // A random sample of the LSH clusters with at least SAMPLE_CLUSTER_MIN_SIZE posts at the MH-14 setting: the
  // clusters whose smallest post index has the smallest XXHash64 (seed SAMPLE_SEED).
  std::ostringstream sample_tsv;
  sample_tsv << tsv_licence() << meta.str() << "# threshold=" << tfix(src.ref_threshold) << "\n"
             << "Sample\tKind\tPost\tOther\tTrueCluster\tExactJ\tEstimatedJ\n";
  std::size_t sample_eligible = 0;
  std::size_t sample_drawn = 0;
  std::size_t sample_merges = 0;
  if (ref != nullptr) {
    std::vector<std::size_t> true_size(n, 0);
    for (const std::size_t label : true_label[t_ref]) {
      ++true_size[label];
    }
    std::vector<std::vector<std::size_t>> eligible;
    std::vector<std::size_t> keys;
    for (auto& cluster : demo::clusters_of(saved[0].lsh_label)) {
      if (cluster.size() >= demo::SAMPLE_CLUSTER_MIN_SIZE) {
        keys.push_back(post_of[cluster.front()]);
        eligible.push_back(std::move(cluster));
      }
    }
    sample_eligible = eligible.size();
    const auto picked = demo::smallest_hashes(keys, demo::SAMPLE_CLUSTERS, demo::SAMPLE_SEED);
    sample_drawn = picked.size();
    for (std::size_t s = 0; s < picked.size(); ++s) {
      const auto& members = eligible[picked[s]];  // ascending
      std::vector<bool> member(n, false);
      std::set<std::size_t> parts;  // true components, a post in no true cluster counts as its own
      for (const std::size_t u : members) {
        member[u] = true;
        const std::size_t label = true_label[t_ref][u];
        parts.insert(label);
        sample_tsv << s + 1 << "\tnode\t" << post_name(u) << "\t-\t" << (true_size[label] >= 2 ? post_name(label) : "-")
                   << "\t-\t-\n";
      }
      sample_merges += parts.size() >= 2 ? 1 : 0;
      std::set<std::pair<std::size_t, std::size_t>> verified_edges;
      for (std::size_t e = 0; e < saved[0].edges.size(); ++e) {
        const auto& edge = saved[0].edges[e];
        if (member[edge.first] && member[edge.second]) {
          verified_edges.insert(edge);
          sample_tsv << s + 1 << "\tverified\t" << post_name(edge.first) << "\t" << post_name(edge.second) << "\t-\t"
                     << fixed(saved[0].exact[e], 4) << "\t" << fixed(saved[0].estimate[e], 4) << "\n";
        }
      }
      // true pairs between drawn posts that LSH did not verify
      for (std::size_t a = 0; a < members.size(); ++a) {
        for (std::size_t b = a + 1; b < members.size(); ++b) {
          const auto it = pair_index.find(static_cast<uint64_t>(members[a]) * n + members[b]);
          if (it != pair_index.end() && exact_j[it->second] >= src.ref_threshold &&
              verified_edges.count({members[a], members[b]}) == 0) {
            sample_tsv << s + 1 << "\tmissed\t" << post_name(members[a]) << "\t" << post_name(members[b]) << "\t-\t"
                       << fixed(exact_j[it->second], 4) << "\t-\n";
          }
        }
      }
    }
  }

  // The recommended (k, b x r) of each t: the smallest k with a setting that meets the target, and at that k
  // the largest min(recall, precision), ties by the smaller r.
  std::vector<const grid_result*> recommended(n_t, nullptr);
  for (std::size_t ti = 0; ti < n_t; ++ti) {
    const grid_result*& best = recommended[ti];
    for (std::size_t ki = 0; ki < demo::FULL_K_VALUES.size() && best == nullptr; ++ki) {
      for (std::size_t ri = 0; ri < n_rows; ++ri) {
        const auto& g = results[(ki * n_rows + ri) * n_t + ti];
        const auto score = [](const grid_result& x) { return std::min(recall_of(x.pairs), precision_of(x.pairs)); };
        if (meets_target(g, src.pair_test_in_overall) && (best == nullptr || score(g) > score(*best))) {
          best = &g;
        }
      }
    }
  }
  // The settings of the held-out check and of the kept-set table: the MH-14 setting, then the recommended
  // setting of each t, each once.
  std::vector<std::pair<std::string, const grid_result*>> settings;
  if (ref != nullptr) {
    settings.emplace_back(src.ref_label, ref);
  }
  for (std::size_t ti = 0; ti < n_t; ++ti) {
    if (recommended[ti] == nullptr) {
      continue;
    }
    const std::string label = "recommended at t = " + tfix(src.thresholds[ti]);
    const auto same = std::find_if(settings.begin(), settings.end(),
                                   [&](const auto& s) { return s.second == recommended[ti]; });
    if (same == settings.end()) {
      settings.emplace_back(label, recommended[ti]);
    } else {
      same->first += ", " + label;
    }
  }

  // Held-out queries: index only the index set, and query with each post of the query set.
  const std::size_t query_size = n - index_size;
  const auto heldout_start = steady::now();
  const auto heldout = run_heldout(in_a, sets, pair_nodes, exact_j, pair_index, settings, test_alpha, low);
  std::cout << "held-out queries: " << fixed(seconds_since(heldout_start), 1) << " s\n";

  std::ostringstream heldout_tsv;
  heldout_tsv << tsv_licence() << meta.str() << "# heldout_seed=" << demo::HELDOUT_SEED << "\n# index_size="
              << index_size << "\n# query_size=" << query_size << "\n"
              << "Setting\tK\tR\tB\tT\tTrueCross\tVerifiedCross\tVerifiedTrueCross\tRecall\tPrecision\tExpectedTrue\t"
                 "ExpectedFalse\tPRecall\tPPrecision\tQueriesWithMatch\tQueriesHit\tHitRate\tInSampleRecall\t"
                 "InSamplePrecision\n";
  for (const auto& h : heldout) {
    const auto& c = h.cross.counts;
    const auto& in = h.in_sample->pairs.counts;
    const auto& gp = h.in_sample->point;
    heldout_tsv << h.setting << "\t" << gp.k << "\t" << gp.rows << "\t" << gp.bands << "\t" << tfix(gp.threshold)
                << "\t" << c.similar << "\t" << c.verified << "\t" << c.both << "\t" << ratio(c.both, c.similar) << "\t"
                << ratio(c.both, c.verified) << "\t" << fixed(h.cross.expected_true, 2) << "\t"
                << fixed(h.cross.expected_false, 2) << "\t" << general(h.cross.p_recall) << "\t"
                << general(h.cross.p_precision) << "\t" << h.queries_with_match << "\t" << h.queries_hit << "\t"
                << ratio(h.queries_hit, h.queries_with_match) << "\t" << ratio(in.both, in.similar) << "\t"
                << ratio(in.both, in.verified) << "\n";
  }

  std::ostringstream kept_tsv;
  kept_tsv << tsv_licence() << meta.str()
           << "K\tR\tB\tT\tDocuments\tKeptExact\tKeptLSH\tKeptBoth\tWronglyRemoved\tMissedDuplicates\tRemovedExact\t"
              "RemovedLSH\tDataLossRate\tMissedDuplicateRate\tLostComponents\tExtraKept\n";
  for (const auto& g : results) {
    const auto& k = g.kept;
    kept_tsv << g.point.k << "\t" << g.point.rows << "\t" << g.point.bands << "\t" << tfix(g.point.threshold)
             << "\t" << k.items << "\t" << k.kept_exact << "\t" << k.kept_lsh << "\t" << k.kept_both << "\t"
             << k.wrongly_removed << "\t" << k.missed_duplicates << "\t" << k.items - k.kept_exact << "\t"
             << k.items - k.kept_lsh << "\t" << ratio(k.wrongly_removed, k.kept_exact) << "\t"
             << ratio(k.missed_duplicates, k.items - k.kept_exact) << "\t" << k.lost_components << "\t" << k.extra_kept
             << "\n";
  }

  std::ostringstream pairs_tsv;
  pairs_tsv << tsv_licence() << meta.str()
            << "K\tR\tB\tT\tMinMatches\tTruePairs\tFalsePairs\tCandidates\tReported\tReportedTrue\tReportedFalse\t"
               "Recall\tPrecision\tExpectedTrue\tExpectedFalse\tPRecall\tPPrecision\tCheck\n";
  std::ostringstream clusters_tsv;
  clusters_tsv << tsv_licence() << meta.str()
               << "K\tR\tB\tT\tLshClusters\tTrueClusters\tLshPairs\tTruePairs\tBothPairs\tPrecision\tRecall\tMerges\t"
                  "Splits\tLargestLsh\tLargestTrue\t"
               << src.external_column << "\tWorstMerges\n";
  for (const auto& g : results) {
    const auto& c = g.pairs.counts;
    const std::string point = std::to_string(g.point.k) + "\t" + std::to_string(g.point.rows) + "\t" +
                              std::to_string(g.point.bands) + "\t" + tfix(g.point.threshold) + "\t";
    pairs_tsv << point << g.min_matches << "\t" << c.similar << "\t" << g.pairs.false_pairs << "\t" << g.candidates
              << "\t" << c.verified << "\t" << c.both << "\t" << c.verified - c.both << "\t" << ratio(c.both, c.similar)
              << "\t" << ratio(c.both, c.verified) << "\t" << fixed(g.pairs.expected_true, 2) << "\t"
              << fixed(g.pairs.expected_false, 2) << "\t" << general(g.pairs.p_recall) << "\t"
              << general(g.pairs.p_precision) << "\t" << verdict(g.pairs.pass) << "\n";
    const auto& cm = g.clusters;
    std::string worst;
    for (const auto& w : g.worst) {
      worst += (worst.empty() ? "" : ";") + std::to_string(w.true_parts) + "x" + std::to_string(w.nodes) + ":" +
               src.names[w.weakest.first] + "|" + src.names[w.weakest.second] + "@" + fixed(w.weakest_j, 4);
    }
    clusters_tsv << point << cm.lsh_clusters << "\t" << cm.true_clusters << "\t" << cm.lsh_pairs << "\t"
                 << cm.true_pairs << "\t" << cm.both_pairs << "\t" << ratio(cm.both_pairs, cm.lsh_pairs) << "\t"
                 << ratio(cm.both_pairs, cm.true_pairs) << "\t" << cm.merges << "\t" << cm.splits << "\t"
                 << cm.largest_lsh << "\t" << cm.largest_true << "\t" << g.external_split << "\t"
                 << (worst.empty() ? "-" : worst) << "\n";
  }
  std::ostringstream estimates_tsv;
  estimates_tsv << tsv_licence() << meta.str() << "# min_j=" << fixed(demo::ESTIMATE_MIN_J, 1) << "\n"
                << "K\tPairs\tMeanError\tRmse\tRmsTheorySd\tRatio\n";
  for (const auto& e : estimates) {
    estimates_tsv << e.k << "\t" << e.pairs << "\t" << fixed(e.mean_error, 6) << "\t" << fixed(e.rmse, 6) << "\t"
                  << fixed(e.rms_sd, 6) << "\t" << (e.rms_sd > 0.0 ? fixed(e.rmse / e.rms_sd, 4) : "NA") << "\n";
  }

  // The Markdown summary.
  std::ostringstream md;
  md << "<!--\n";
  for (const char* line : LICENCE_LINES) {
    md << line << "\n";
  }
  md << "-->\n\n";
  md << "# Full-data precision and recall of the sketch and the LSH clusters on " << src.dataset
     << (smoke ? " (smoke run)" : "") << "\n\n";
  md << "Written by `build/" << src.stem << "` (`FULL=1 ./run_demos.sh`). The ticket is " << src.ticket
     << ". The tables for every grid point are in `" << src.stem << "_pairs.tsv`, `" << src.stem
     << "_clusters.tsv` and `" << src.stem << "_estimates.tsv`. `plots/lsh_20news_full.py` draws the figures from "
        "them.\n\n";
  md << "## Definitions\n\n";
  if (src.shingle_words == 1) {
    md << "- **Data point:** one " << src.noun << ". It is the set of its words: the runs of lower-case letters and "
          "digits that `demo::words` finds, " << src.shingle_source << ". An indexed " << src.noun << " has at least "
          "1 word.\n";
  } else {
    md << "- **Data point:** one " << src.noun << ". It is the set of its " << src.shingle_words
       << "-word shingles: runs of " << src.shingle_words << " consecutive lower-case words, " << src.shingle_source
       << ". An indexed " << src.noun << " has at least 1 shingle.\n";
  }
  md << "- **Pair:** two different indexed " << src.nouns << ", in any order.\n";
  md << "- **Exact J:** |A ∩ B| / |A ∪ B| of the two shingle sets. **Estimated J (Ĵ):** the share of the k slots "
        "whose values are equal.\n";
  md << "- **True pair at t:** a pair with exact J ≥ t.\n";
  md << "- **Candidate pair:** a pair whose band keys are equal in at least 1 band.\n";
  md << "- **Verified pair:** a candidate pair with Ĵ ≥ t. The TSV columns `Reported`, `ReportedTrue` and "
        "`ReportedFalse`, and the word \"reported\" in the figures, mean the verified pairs.\n";
  md << "- **Pair recall:** verified true pairs ÷ true pairs. **Pair precision:** verified true pairs ÷ verified "
        "pairs.\n";
  md << "- **True cluster:** a connected group of 2 or more " << src.nouns << " that true pairs link. **LSH cluster:** a connected "
        "group of 2 or more " << src.nouns << " that verified pairs link (`get_duplicate_clusters()`).\n";
  md << "- **Co-membership pair:** two " << src.nouns << " in the same cluster. It includes " << src.nouns << " that only a chain links, so "
        "their exact J can be below t.\n";
  md << "- **Cluster recall:** the co-membership pairs that are in the true clusters and in the LSH clusters ÷ the "
        "co-membership pairs of the true clusters. **Cluster precision:** the same count ÷ the co-membership pairs "
        "of the LSH clusters. These are not the same quantities as pair recall and pair precision, because a "
        "cluster counts every pair that a chain links.\n";
  md << "- **Merge:** an LSH cluster that joins 2 or more true clusters. Here a " << src.noun << " in no true cluster counts as a "
        "true cluster of its own. **Split:** a true cluster that LSH divides into 2 or more parts.\n";
  md << "- **Index set and query set:** the held-out check splits the indexed " << src.nouns << " in two. A " << src.noun << " goes to the "
        "index set when XXHash64 of its " << src.noun << " index (8 bytes, seed "
     << demo::HELDOUT_SEED
     << ") is even, and to the query set otherwise. **Cross pair:** a pair with one " << src.noun << " in each set. **Query hit "
        "rate:** of the queries with at least 1 true cross pair, the share that get at least 1 verified true cross "
        "pair.\n";
  md << "- **Kept set:** the " << src.nouns << " that deduplication keeps: the smallest " << src.noun << " of each cluster, and every " << src.noun << " in "
        "no cluster. **Wrongly removed:** a " << src.noun << " that the exact clusters keep and the LSH clusters remove. **Missed "
        "duplicate:** a " << src.noun << " that the LSH clusters keep and the exact clusters remove. **Data-loss rate:** wrongly "
        "removed ÷ kept by the exact clusters. **Missed-duplicate rate:** missed duplicates ÷ removed by the exact "
        "clusters. These compare which " << src.noun << " each side keeps. When a merge and a split meet, LSH can keep "
        "another " << src.noun << " of the same true cluster, so both counts are upper bounds. **Lost true cluster:** "
        "a true cluster, or a " << src.noun << " in no true cluster, with no " << src.noun
     << " in the LSH kept set: real data loss. **Extra kept:** the LSH-kept " << src.nouns
     << " beyond one for each true cluster that they cover: near-copies really left in.\n\n";
  md << "## How deduplication works\n\n";
  md << "MinHash does not store the " << src.nouns << " or their items. The index keeps a copy of each " << src.noun << "'s sketch, which is "
        "its k slot values, and a table of band keys, so that it can find likely near-duplicates fast. What to do "
        "with them, here deduplication, is the next step.\n\n";
  md << "1. LSH finds the candidate pairs, and the program estimates Ĵ for each one.\n";
  md << "2. A pair with Ĵ ≥ t is a near-duplicate, so it becomes a link. A pair below t is not a duplicate, so no "
        "link is made. The pruning removes links, not documents.\n";
  md << "3. The links form clusters, which are groups of near-copies.\n";
  md << "4. Deduplication keeps one document from each cluster and removes the others. A document in no cluster is "
        "kept as it is.\n";
  md << "5. The downstream task, for example training or analysis, gets the kept documents.\n\n";
  md << "## LSH guarantee\n\n";
  md << "An LSH family is (r, cr, p1, p2)-sensitive if pairs at distance d ≤ r collide with probability at least "
        "p1, and pairs at distance d ≥ cr collide with probability at most p2. For MinHash the distance is the "
        "Jaccard distance d = 1 − J. One slot collides with probability exactly J = 1 − d, so p1 = 1 − r and "
        "p2 = 1 − cr. Bands amplify it. A pair is a candidate with probability f(J) = 1 − (1 − J^rows)^b. This "
        "curve rises with J, so for any r and c, p1 = f(1 − r) and p2 = f(1 − c·r). For example, at 32 bands × 4 "
        "rows: d ≤ 0.3 (J ≥ 0.7) gives p1 ≥ 0.9998; d ≥ 0.7 (J ≤ 0.3) gives p2 ≤ 0.2292.\n\n";
  md << "In this definition, r is a distance radius. It is not the rows per band (r) of the grid. The S-curve check "
        "(MH-14, MH-19) and the pair check of this report test f as an equality at many values of J, which is "
        "stronger than the two inequalities.\n\n";
  md << "## Data\n\n";
  md << "- " << src.nouns << ": " << src.chosen << (smoke ? " (" + src.sample_note + ")" : "") << "\n";
  md << "- indexed " << src.nouns << " (at least one shingle of " << src.shingle_words << (src.shingle_words == 1 ? " word" : " words") << "): " << n << "\n";
  md << "- pairs of indexed " << src.nouns << ": " << n * (n - 1) / 2 << "\n";
  if (src.floor > 0.0) {
    md << "- pairs with exact J ≥ " << tfix(src.floor) << " (stored in the exact-pair cache): " << n_pairs << "\n";
    md << "- pairs with 0 < J < " << tfix(src.floor) << " (counted in J bins of width " << general(LOW_BIN_WIDTH)
       << ", not stored): " << low_count << "\n";
  } else {
    md << "- pairs with exact J > 0 (stored in the exact-pair cache): " << n_pairs << "\n";
  }
  std::string rows_text;
  for (const unsigned rows : src.rows) {
    rows_text += (rows_text.empty() ? "" : ", ") + std::to_string(rows);
  }
  md << "- grid: k ∈ {64, 128, 256, 512, 1024}, r ∈ {" << rows_text << "}, b = k / r, t ∈ {"
     << tfix(src.thresholds.front()) << ", …, " << tfix(src.thresholds.back()) << "}: " << grid.size()
     << " points\n\n";
  md << "Truth: a true pair at t has exact J ≥ t, with J from `news20::jaccard` on " << src.shingle_sets
     << ". The true clusters at t are the connected components of the true pairs. Verified pairs: the candidates "
        "(equal band keys in at least one band) with Ĵ ≥ t. The LSH clusters are `get_duplicate_clusters()`. The "
        "program asserts, at each grid point, that the components of the verified pairs equal them.\n\n";

  md << "## Prefix assert: " << verdict(prefix_pass) << "\n\n";
  md << "Each sketch is built directly at its k. For each k < 1024 and each indexed " << src.noun
     << ", the k signature must equal the first k slots of the k = 1024 signature.\n\n";
  md << "- signatures checked: " << prefix_checked << "\n- signatures that differ: " << prefix_failed << "\n\n";

  md << "## " << src.external_title << ": " << verdict(external_pass) << "\n\n";
  md << src.external_text << "\n\n";
  md << "- " << src.external_label << ": " << src.external.size()
     << "\n- (pair, grid point) cases not in one cluster: " << external_failed << "\n\n";

  md << "## Pair check: " << (!src.pair_test_in_overall ? "report only" : pair_failed == 0 ? "PASS" : "FAIL") << " ("
     << grid.size() - pair_failed << " of " << grid.size() << " grid points pass)\n\n";
  md << "P(verified | J) is the joint probability that some band is all-equal and at least m = ⌈t k⌉ slots are "
        "equal, with each slot equal independently with probability J. It is Σ_{s ≥ m} P(Binomial(k, J) = s) q(s), "
        "where q(s) is the probability that s uniformly placed equal slots fill some band (a dynamic program over the "
        "bands). At each grid point, two counts are tested with the exact two-sided Poisson-binomial p-value: the "
        "verified true pairs against Σ P(verified | J) over the true pairs (recall), and the verified false pairs "
        "against Σ P(verified | J) over the pairs with 0 < J < t (precision). Pairs with J = 0 add 0. A test passes "
        "when p ≥ "
     << general(demo::FULL_ALPHA) << " / " << 2 * grid.size() << " = " << general(test_alpha)
     << ". If the pairs were independent, a correct index would fail some test with a probability of at most "
     << general(demo::FULL_ALPHA)
     << ". They are not: pairs that share a " << src.noun
     << " use the same signature, so their outcomes are positively correlated, the counts spread more than the "
        "Poisson-binomial allows, and the p-values are too small. So this bound is not exact. ";
  if (src.pair_test_in_overall) {
    md << "The margin here is wide: the smallest p-value is in `" << src.stem
       << "_pairs.tsv` and the figure below. MH-14 checks the S-curve on independent synthetic pairs for this "
          "reason.\n\n";
  } else {
    md << "Here the pair check is report only, and it does not enter `Overall`. MH-14 checks the S-curve on "
          "independent synthetic pairs.\n\n";
  }
  if (src.floor > 0.0) {
    md << "The false count also includes the pairs below the floor: each bin adds its count times P(verified | J) at "
          "the middle of the bin. Because the exact Poisson-binomial program is too slow for so many pairs, the "
          "precision p-value is the two-sided normal approximation with a continuity correction. The recall p-value "
          "stays exact, because every true pair is stored.\n\n";
  }
  md << src.pair_test_note;
  md << "![Measured counts against the theory, and all p-values](" << src.stem << "_theory.svg)\n\n";
  md << "How to read it: on the left, each dot is one count at one grid point. A dot on the line means that the "
        "measured count equals the theory. On the right, the p-values of all tests are sorted. A test fails only "
        "below the red line.\n\n";
  if (pair_failed > 0) {
    md << "| k | r | t | recall p | precision p |\n|---:|---:|---:|---:|---:|\n";
    for (const auto& g : results) {
      if (!g.pairs.pass) {
        md << "| " << g.point.k << " | " << g.point.rows << " | " << tfix(g.point.threshold) << " | "
           << general(g.pairs.p_recall) << " | " << general(g.pairs.p_precision) << " |\n";
      }
    }
    md << "\n";
  }

  md << "## Recommended k\n\n";
  md << "For each t: the smallest k with a (b, r) whose pair recall and pair precision are both ≥ "
     << fixed(demo::FULL_TARGET, 2)
     << (src.pair_test_in_overall ? " and whose pair check passes" : "")
     << ". The best (b, r) at that k has the largest min(recall, precision), ties by the smaller r.\n\n";
  md << "| t | k | b × r | recall | precision |\n|---:|---:|---|---:|---:|\n";
  for (std::size_t ti = 0; ti < n_t; ++ti) {
    const grid_result* best = recommended[ti];
    md << "| " << tfix(src.thresholds[ti]) << " | ";
    if (best == nullptr) {
      md << "none reaches the target | - | - | - |\n";
    } else {
      md << best->point.k << " | " << best->point.bands << " × " << best->point.rows << " | "
         << ratio(best->pairs.counts.both, best->pairs.counts.similar) << " | "
         << ratio(best->pairs.counts.both, best->pairs.counts.verified) << " |\n";
    }
  }
  md << "\n";

  md << "## Pair recall and precision at each grid point\n\n";
  md << "![Recall and precision at every grid point](" << src.stem << "_heatmap.svg)\n\n";
  md << "How to read it: each row is one (k, b × r), and each column is one t. Bright cells are near 1, and bold "
        "values are at least 0.95. Recall rises and precision falls as r gets smaller.\n\n";
  md << "![Precision against recall for each k](" << src.stem << "_pr.svg)\n\n";
  md << "How to read it: one panel for each k, and one curve for each r. Each curve runs from t = "
     << tfix(src.thresholds.front()) << " to t = " << tfix(src.thresholds.back()) << ". "
        "The dotted lines mark 0.95, so a point in the top-right corner meets the target of a recommended k.\n\n";
  md << "Each cell is recall / precision. A FAIL marks a failed pair check.\n\n| k | b × r |";
  for (const double t : src.thresholds) {
    md << " t = " << tfix(t) << " |";
  }
  md << "\n|---:|---|";
  for (std::size_t ti = 0; ti < n_t; ++ti) {
    md << "---|";
  }
  md << "\n";
  for (std::size_t g0 = 0; g0 < results.size(); g0 += n_t) {
    md << "| " << results[g0].point.k << " | " << results[g0].point.bands << " × " << results[g0].point.rows << " |";
    for (std::size_t ti = 0; ti < n_t; ++ti) {
      const auto& g = results[g0 + ti];
      md << " " << ratio(g.pairs.counts.both, g.pairs.counts.similar) << " / "
         << ratio(g.pairs.counts.both, g.pairs.counts.verified) << (g.pairs.pass ? "" : " FAIL") << " |";
    }
    md << "\n";
  }
  md << "\n";

  md << "## Verified rate for each J band\n\n";
  md << "Report only. The pairs with J > 0 are cut into bands of width 0.1, and J = 1 is in the last band. The "
        "verified rate of a band depends only on the J values of its pairs, not on the mix of pairs in a dataset, so "
        "it is the figure to compare across datasets and with the theory. In a band at or above t, the verified rate "
        "is the recall for pairs of that J. In a band below t, it is the false-verify rate. The expected rate is the "
        "mean of P(verified | J) over the pairs of the band. All grid points are in `"
     << src.stem << "_bands.tsv`.\n\n";
  if (ref != nullptr) {
    md << "At k = " << src.ref_k << ", " << ref_bands << " × " << src.ref_rows << ", t = " << tfix(src.ref_threshold)
       << ":\n\n";
    md << "| J band | pairs | verified | verified rate | expected rate |\n|---|---:|---:|---:|---:|\n";
    for (std::size_t band = 0; band < demo::NUM_BINS; ++band) {
      const auto& b = ref->bands[band];
      const auto count = static_cast<double>(b.pairs);
      md << "| " << band_label(band) << " | " << b.pairs << " | " << b.verified << " | "
         << (b.pairs == 0 ? "-" : fixed(static_cast<double>(b.verified) / count, 4)) << " | "
         << (b.pairs == 0 ? "-" : fixed(b.expected / count, 4)) << " |\n";
    }
    md << "\n";
  }
  md << "![Verified rate for each J band](" << src.stem << "_bands.svg)\n\n";
  md << "How to read it: the dots are the measured verified rate of each band, and the line is the expected rate. "
        "The dotted line marks t. Right of it, the rate is the recall for pairs of that J; left of it, the rate is "
        "the false-verify rate. The right panel shows the same rates on a log scale, so small rates are visible "
        "(a band with no verified pair is not drawn there).\n\n";
  md << src.bands_extra;
  md << "## Held-out queries\n\n";
  md << "The sections above index every " << src.noun << " and cluster them, which is the use case \"find near-copies inside a "
        "corpus\". This section tests the other use case: build the index once, then query it with " << src.nouns << " that it "
        "has never seen. The index holds only the index set ("
     << index_size << " " << src.nouns << "). Each " << src.noun << " of the query set (" << query_size
     << " " << src.nouns << ") calls `query_candidates()`, and the candidates with Ĵ ≥ t are verified. The program asserts that "
        "the candidates equal the band-key bucket rule computed in the demo. Only the cross pairs count. The "
        "p-values are report only, with no PASS or FAIL. The data is in `" << src.stem << "_heldout.tsv`.\n\n";
  md << "| setting | k | b × r | t | true cross pairs | query recall | in-sample recall | query precision | "
        "in-sample precision | verified true: measured / expected | verified false: measured / expected | "
        "p recall | p precision | query hit rate |\n";
  md << "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
  for (const auto& h : heldout) {
    const auto& c = h.cross.counts;
    const auto& in = h.in_sample->pairs.counts;
    const auto& gp = h.in_sample->point;
    md << "| " << h.setting << " | " << gp.k << " | " << gp.bands << " × " << gp.rows << " | " << tfix(gp.threshold)
       << " | " << c.similar << " | " << ratio(c.both, c.similar) << " | " << ratio(in.both, in.similar) << " | "
       << ratio(c.both, c.verified) << " | " << ratio(in.both, in.verified) << " | " << c.both << " / "
       << fixed(h.cross.expected_true, 1) << " | " << c.verified - c.both << " / " << fixed(h.cross.expected_false, 1)
       << " | " << general(h.cross.p_recall) << " | " << general(h.cross.p_precision) << " | "
       << ratio(h.queries_hit, h.queries_with_match) << " (" << h.queries_hit << " of " << h.queries_with_match
       << ") |\n";
  }
  md << "\nHow to read it: query recall and precision should be close to the in-sample figures of the same "
        "setting, because a query " << src.noun << " meets the same band keys and the same Ĵ test as an indexed " << src.noun << ". The "
        "measured and expected counts should agree, as in the pair check. The query hit rate says how often a new "
        "" << src.noun << " with a near-copy in the index finds at least one of them.\n\n";
  md << "## Estimate error\n\n";
  md << "On the stored pairs with exact J ≥ " << tfix(std::max(demo::ESTIMATE_MIN_J, src.floor))
     << ". The ratio is the RMSE divided by the RMS of √(J (1 − J) / k), so 1 is the theory.\n\n";
  md << "![Estimate error against k](" << src.stem << "_estimates.svg)\n\n";
  md << "How to read it: on the left, the measured RMSE (blue) lies on the theory (orange), and it halves when k "
        "grows 4 times. The bias (green) is much smaller than the RMSE. On the right, the ratio is near 1 at every "
        "k.\n\n";
  md << "| k | pairs | mean error | RMSE | RMS theory SD | ratio |\n|---:|---:|---:|---:|---:|---:|\n";
  for (const auto& e : estimates) {
    md << "| " << e.k << " | " << e.pairs << " | " << fixed(e.mean_error, 6) << " | " << fixed(e.rmse, 6) << " | "
       << fixed(e.rms_sd, 6) << " | " << (e.rms_sd > 0.0 ? fixed(e.rmse / e.rms_sd, 4) : "NA") << " |\n";
  }
  md << "\n";

  md << "## Clusters at k = " << src.ref_k << ", " << ref_bands << " × " << src.ref_rows << ", t = "
     << tfix(src.ref_threshold) << " (the " << src.ref_label << ")\n\n";
  md << "Co-membership: a pair of " << src.nouns << " is in one cluster. A " << src.noun
     << " alone counts as a component of its own, so a merge can join a true cluster and a " << src.noun
     << " that is in no true cluster. All grid points are in `" << src.stem << "_clusters.tsv`.\n\n";
  md << "![Cluster precision against recall for each k](" << src.stem << "_clusters.svg)\n\n";
  md << "How to read it: as the pair figure, but for pairs of " << src.nouns
     << " in one cluster of `get_duplicate_clusters()` against pairs in one true cluster.\n\n";
  if (ref != nullptr) {
    const auto& cm = ref->clusters;
    md << "- LSH clusters: " << cm.lsh_clusters << "; true clusters: " << cm.true_clusters << "\n";
    md << "- co-membership precision: " << ratio(cm.both_pairs, cm.lsh_pairs) << " (" << cm.both_pairs << " of "
       << cm.lsh_pairs << " pairs)\n";
    md << "- co-membership recall: " << ratio(cm.both_pairs, cm.true_pairs) << " (" << cm.both_pairs << " of "
       << cm.true_pairs << " pairs)\n";
    md << "- merges (LSH clusters that join 2 or more true components): " << cm.merges << "\n";
    md << "- splits (true clusters that LSH splits): " << cm.splits << "\n";
    md << "- largest LSH cluster: " << cm.largest_lsh << " " << src.nouns << "; largest true cluster: "
       << cm.largest_true << " " << src.nouns << "\n\n";
    md << "The " << demo::WORST_MERGES
       << " worst merges (most true components, then most " << src.nouns
       << "), each with its weakest link: the verified pair of the cluster with the lowest exact J.\n\n";
    md << "| true components | " << src.nouns
       << " | weakest link | exact J | 3 shared shingles |\n|---:|---:|---|---:|---|\n";
    for (const auto& w : ref->worst) {
      md << "| " << w.true_parts << " | " << w.nodes << " | " << name(w.weakest.first) << ", " << name(w.weakest.second)
         << " | " << fixed(w.weakest_j, 4) << " | " << shared_shingles(sets[w.weakest.first], sets[w.weakest.second])
         << " |\n";
    }
    md << "\n";
    md << "![The worst merges as graphs](" << src.stem << "_merges.svg)\n\n";
    md << "How to read it: each column is one merge of the table above, in the same order. Each dot is a " << src.noun
       << ", coloured by its true cluster at t = " << tfix(src.ref_threshold)
       << ". Each line is a verified pair, labelled with its exact J. A grey line joins " << src.nouns
       << " of one true cluster. A red line has J < t, so it is a false link that joins 2 true clusters, and the "
          "thickest red line is the weakest link. The top row is the " << src.ref_label << " (k = "
       << src.ref_k << ", " << ref_bands << " × " << src.ref_rows << "). The bottom row shows the same "
       << src.nouns << " at k = " << src.compare_k << ", " << src.compare_k / src.compare_rows << " × " << src.compare_rows
       << ", with only the pairs among these " << src.nouns << ". The data is in `" << src.stem
       << "_merges.tsv`.\n\n";
    md << "### A random sample of the clusters\n\n";
    md << sample_drawn << " of the " << sample_eligible << " LSH clusters with at least " << demo::SAMPLE_CLUSTER_MIN_SIZE
       << " " << src.nouns << " at this setting, chosen by the smallest XXHash64 (seed " << demo::SAMPLE_SEED
       << ") of the smallest " << src.noun << " index of each cluster. " << sample_merges << " of them contain a merge.\n\n";
    md << "![A random sample of the clusters as graphs](" << src.stem << "_sample_clusters.svg)\n\n";
    md << "How to read it: each panel is one LSH cluster. Each dot is a " << src.noun << ", coloured by its true cluster at t = "
       << tfix(src.ref_threshold)
       << ", and a grey dot is in no true cluster. A solid line is a verified pair, labelled with its exact J, and "
          "it is red when J < t. A dashed line is a true pair (J ≥ t) between drawn " << src.nouns << " that LSH did not verify; "
          "the chain still puts both " << src.nouns << " in the cluster. The data is in `" << src.stem << "_sample_clusters.tsv`.\n\n";
  }

  md << "## Kept set (deduplication)\n\n";
  md << "The same deduplication runs twice at each grid point: once on the true clusters, and once on the LSH "
        "clusters. Each keeps the smallest " << src.noun << " of each cluster and every " << src.noun << " in no cluster. The comparison shows "
        "what LSH costs a downstream task: " << src.nouns << " lost by mistake, and near-copies left in. All grid points are in "
        "`" << src.stem << "_kept.tsv`.\n\n";
  md << "| setting | k | b × r | t | " << src.nouns << " | kept (exact) | kept (LSH) | wrongly removed | missed duplicates | "
        "data-loss rate | missed-duplicate rate | lost true clusters | extra kept |\n"
        "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
  for (const auto& setting : settings) {
    const auto& g = *setting.second;
    const auto& k = g.kept;
    md << "| " << setting.first << " | " << g.point.k << " | " << g.point.bands << " × " << g.point.rows << " | "
       << tfix(g.point.threshold) << " | " << k.items << " | " << k.kept_exact << " | " << k.kept_lsh << " | "
       << k.wrongly_removed << " | " << k.missed_duplicates << " | " << ratio(k.wrongly_removed, k.kept_exact) << " | "
       << ratio(k.missed_duplicates, k.items - k.kept_exact) << " | " << k.lost_components << " | " << k.extra_kept
       << " |\n";
  }
  md << "\n![Data-loss rate against missed-duplicate rate](" << src.stem << "_kept.svg)\n\n";
  md << "How to read it: one panel for each r, and one curve for each k, with a point for each t. Lower left is "
        "good. A point high on the y axis removes " << src.nouns << " that have no near-copy (data loss, mostly from merges). A "
        "point far right keeps near-copies that should go (missed duplicates, mostly from splits).\n\n";
  std::ostringstream proxy_tsv;
  if (has_proxy) {
    md << proxy_section(src, results, n_t, true_proxy_pairs, proxy_true, meta.str(), ref_bands, proxy_tsv);
  }

  md << "## The " << TOP_PAIRS << " true pairs with the highest J\n\n";
  md << "Ties by " << src.noun << " index. The shingles show whether quoted text or boilerplate makes the pairs "
        "similar.\n\n";
  md << "| " << src.nouns << " | exact J | 3 shared shingles |\n|---|---:|---|\n";
  std::vector<std::size_t> order(n_pairs);
  for (std::size_t e = 0; e < n_pairs; ++e) {
    order[e] = e;
  }
  const std::size_t top = std::min(TOP_PAIRS, n_pairs);
  std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(top), order.end(),
                    [&exact_j, &pair_nodes](std::size_t a, std::size_t b) {
                      return exact_j[a] != exact_j[b] ? exact_j[a] > exact_j[b] : pair_nodes[a] < pair_nodes[b];
                    });
  for (std::size_t r = 0; r < top; ++r) {
    const auto& pr = pair_nodes[order[r]];
    md << "| " << name(pr.first) << ", " << name(pr.second) << " | " << fixed(exact_j[order[r]], 4) << " | "
       << shared_shingles(sets[pr.first], sets[pr.second]) << " |\n";
  }
  md << "\n";
  md << "Overall: " << verdict(overall) << "\n";

  // The merge figure data: the items of each worst merge at the MH-14 setting, and the reported pairs among them.
  std::ostringstream merges_tsv;
  merges_tsv << tsv_licence() << meta.str() << "# threshold=" << tfix(src.ref_threshold) << "\n"
             << "Merge\tKind\tSetting\tPost\tOther\tTrueCluster\tExactJ\tEstimatedJ\n";
  if (ref != nullptr) {
    const std::array<std::string, 2> setting{
        "k" + std::to_string(src.ref_k) + "_b" + std::to_string(ref_bands) + "_r" + std::to_string(src.ref_rows),
        "k" + std::to_string(src.compare_k) + "_b" + std::to_string(src.compare_k / src.compare_rows) + "_r" +
            std::to_string(src.compare_rows)};
    for (std::size_t w = 0; w < ref->worst.size(); ++w) {
      std::vector<bool> member(n, false);
      for (std::size_t u = 0; u < n; ++u) {
        if (saved[0].lsh_label[u] == ref->worst[w].lsh_root) {
          member[u] = true;
          merges_tsv << w + 1 << "\tnode\t-\t" << post_name(u) << "\t-\t" << post_name(true_label[t_ref][u])
                     << "\t-\t-\n";
        }
      }
      for (std::size_t s = 0; s < saved.size(); ++s) {
        for (std::size_t e = 0; e < saved[s].edges.size(); ++e) {
          const auto& edge = saved[s].edges[e];
          if (member[edge.first] && member[edge.second]) {
            merges_tsv << w + 1 << "\tedge\t" << setting[s] << "\t" << post_name(edge.first) << "\t"
                       << post_name(edge.second) << "\t-\t" << fixed(saved[s].exact[e], 4) << "\t"
                       << fixed(saved[s].estimate[e], 4) << "\n";
          }
        }
      }
    }
  }

  write_file(out_dir / (src.stem + "_merges.tsv"), merges_tsv.str());
  if (has_proxy) {
    write_file(out_dir / (src.stem + "_proxy.tsv"), proxy_tsv.str());
  }
  write_file(out_dir / (src.stem + "_bands.tsv"), bands_tsv.str());
  write_file(out_dir / (src.stem + "_sample_clusters.tsv"), sample_tsv.str());
  write_file(out_dir / (src.stem + "_heldout.tsv"), heldout_tsv.str());
  write_file(out_dir / (src.stem + "_kept.tsv"), kept_tsv.str());
  write_file(out_dir / (src.stem + "_pairs.tsv"), pairs_tsv.str());
  write_file(out_dir / (src.stem + "_clusters.tsv"), clusters_tsv.str());
  write_file(out_dir / (src.stem + "_estimates.tsv"), estimates_tsv.str());
  write_file(out_dir / (src.stem + ".md"), md.str());

  std::cout << "prefix assert: " << verdict(prefix_pass) << " (" << prefix_failed << " of " << prefix_checked
            << " differ)\n";
  std::cout << src.external_title << ": " << verdict(external_pass) << "\n";
  std::cout << "pair check: " << grid.size() - pair_failed << " of " << grid.size() << " pass\n";
  for (const auto& g : results) {
    if (!g.pairs.pass) {
      std::cout << "  FAIL k = " << g.point.k << ", r = " << g.point.rows << ", t = " << tfix(g.point.threshold)
                << ": recall p " << general(g.pairs.p_recall) << ", precision p " << general(g.pairs.p_precision)
                << "\n";
    }
  }
  std::cout << "total: " << fixed(seconds_since(start), 1) << " s\n";
  std::cout << "Overall: " << verdict(overall) << "\n";
  return 0;
}

}  // namespace

std::vector<std::size_t> full_chosen(std::size_t items, bool smoke) {
  if (smoke) {
    return sample_indices(items, SAMPLE_SIZE, SAMPLE_SEED);
  }
  std::vector<std::size_t> all(items);
  for (std::size_t p = 0; p < items; ++p) {
    all[p] = p;
  }
  return all;
}

int full_main(int argc, char** argv, const char* data_name, const full_loader& load) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const bool smoke = !args.empty() && args[0] == "--smoke";
  if (args.size() != (smoke ? 3U : 2U)) {
    std::cerr << "usage: " << argv[0] << " [--smoke] <" << data_name << " directory> <results directory>\n";
    return 2;
  }
  try {
    const auto start = steady::now();
    fs::path data_dir(args[smoke ? 1 : 0]);
    if (!data_dir.has_filename()) {
      data_dir = data_dir.parent_path();  // a trailing "/"
    }
    return run(load(data_dir, smoke), smoke, data_dir, args[smoke ? 2 : 1], start);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace demo

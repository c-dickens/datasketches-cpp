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

// MH-14b: the sketch and the LSH clusters against the exact Jaccard of every pair of indexed
// 20 Newsgroups posts, at every (k, r, t) of demo::full_grid().
// Usage: lsh_20news_full [--smoke] <20_newsgroups directory> <results directory>
//
// The exact pairs are cached in lsh_20news_full_exact_pairs.tsv (with --smoke:
// lsh_20news_full_exact_pairs_smoke.tsv) next to the 20_newsgroups directory. A cache with the
// right header is loaded; otherwise the program runs the exact pass and writes a new cache.
// --smoke uses only the demo::SAMPLE_SIZE posts of demo::sample_indices().

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

// The SHA-256 of 20_newsgroups.tar.gz: UCI_TAR_SHA256 in data/get_20news.sh, which checks it.
const char* const ARCHIVE_SHA256 = "ab7bbf82b7831f7dbb1a09d9312f66fa78565c8de25526999b0d66f69d37e414";

constexpr std::size_t TOP_PAIRS = 20;
constexpr std::size_t SHARED_SHINGLES = 3;
// The merge figure shows the worst merges of the MH-14 setting, and the same posts at this larger k
// (the recommended k at t = demo::LSH_THRESHOLD in the report).
constexpr uint16_t COMPARE_K = 512;
constexpr unsigned COMPARE_ROWS = 2;

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
  std::size_t cross_split = 0;  // cross-post pairs not in one true cluster, or not in one LSH cluster
  std::array<demo::band_rate, demo::NUM_BINS> bands{};  // the verified pairs of each J band
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

bool meets_target(const grid_result& g) {
  return g.pairs.pass && g.pairs.counts.similar > 0 && g.pairs.counts.verified > 0 &&
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

int run(bool smoke, const std::string& data_arg, const std::string& out_arg) {
  const auto start = steady::now();
  fs::path data_dir(data_arg);
  if (!data_dir.has_filename()) {
    data_dir = data_dir.parent_path();  // a trailing "/"
  }
  const fs::path out_dir(out_arg);
  fs::create_directories(out_dir);

  const auto posts = news20::load_posts(data_dir);
  std::vector<std::size_t> chosen;
  if (smoke) {
    chosen = demo::sample_indices(posts.size(), demo::SAMPLE_SIZE, demo::SAMPLE_SEED);
  } else {
    for (std::size_t p = 0; p < posts.size(); ++p) {
      chosen.push_back(p);
    }
  }
  // The nodes are the indexed posts (at least one shingle). post_of[u] is the index in load_posts order.
  std::vector<std::size_t> post_of;
  std::vector<shingle_set> sets;
  std::vector<std::string> ids;
  for (const std::size_t p : chosen) {
    auto set = news20::shingle_text(posts[p].body);
    if (!set.empty()) {
      post_of.push_back(p);
      sets.push_back(std::move(set));
      ids.push_back(posts[p].message_id);
    }
  }
  const std::size_t n = post_of.size();
  std::vector<std::size_t> node_of(posts.size(), n);
  for (std::size_t u = 0; u < n; ++u) {
    node_of[post_of[u]] = u;
  }
  const auto post_name = [&posts, &post_of](std::size_t u) {
    return posts[post_of[u]].group + "/" + posts[post_of[u]].id;
  };
  const auto name = [&post_name](std::size_t u) { return "`" + post_name(u) + "`"; };
  std::cout << "posts " << chosen.size() << ", indexed " << n << " (" << fixed(seconds_since(start), 1) << " s)\n";

  // The exact pass, or its cache.
  const fs::path cache = data_dir.parent_path() /
      (smoke ? "lsh_20news_full_exact_pairs_smoke.tsv" : "lsh_20news_full_exact_pairs.tsv");
  const std::string header =
      demo::exact_pairs_header(ARCHIVE_SHA256, news20::SHINGLE_WORDS, n, demo::input_fingerprint(post_of, sets));
  const auto exact_start = steady::now();
  std::vector<demo::exact_pair> exact;
  bool loaded = false;
  {
    std::ifstream in(cache, std::ios::binary);
    loaded = in && demo::read_exact_pairs(in, header, exact);
  }
  if (loaded) {
    std::cout << "exact pairs: loaded " << exact.size() << " from " << cache.string() << "\n";
  } else {
    std::cout << "exact pairs: no cache with this header, running the exact pass\n";
    for (std::size_t u = 0; u < n; ++u) {
      for (std::size_t v = u + 1; v < n; ++v) {
        const double j = news20::jaccard(sets[u], sets[v]);
        if (j > 0.0) {
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
        std::cerr << "  exact pass: " << u + 1 << " of " << n << " posts, " << exact.size() << " pairs, "
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
      throw std::runtime_error("the cache has a post that is not indexed");
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

  // Cross-posts: the pairs of indexed posts that share a Message-ID.
  const auto cross = demo::check_pairs(ids, 0, 0);

  // The true clusters at each t.
  std::vector<std::vector<std::size_t>> true_label;
  for (const double t : demo::FULL_THRESHOLDS) {
    demo::edge_list edges;
    for (std::size_t e = 0; e < n_pairs; ++e) {
      if (exact_j[e] >= t) {
        edges.push_back(pair_nodes[e]);
      }
    }
    true_label.push_back(demo::components(n, edges));
  }

  const auto grid = demo::full_grid();
  const double test_alpha = demo::FULL_ALPHA / static_cast<double>(2 * grid.size());
  const std::size_t n_rows = demo::FULL_ROWS.size();
  const std::size_t n_t = demo::FULL_THRESHOLDS.size();
  std::vector<grid_result> results(grid.size());
  // The reported pairs of the MH-14 setting [0] and of COMPARE_K, COMPARE_ROWS [1], at t = LSH_THRESHOLD.
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
    for (const unsigned rows : demo::FULL_ROWS) {
      q.push_back(demo::full_band_given_matches(rows, k / rows));
    }
    std::vector<std::size_t> m;
    for (const double t : demo::FULL_THRESHOLDS) {
      m.push_back(demo::min_matches(t, k));
    }
    std::vector<std::vector<double>> prob(n_rows * n_t, std::vector<double>(n_pairs));
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
      const unsigned rows = demo::FULL_ROWS[ri];
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
        const double t = demo::FULL_THRESHOLDS[ti];
        grid_result& g = results[((ki - 1) * n_rows + ri) * n_t + ti];
        g.point = demo::grid_point{k, rows, bands, t};
        g.min_matches = m[ti];
        g.candidates = candidates.size();
        // The reported pairs: candidates with Ĵ >= t.
        std::vector<bool> reported(n_pairs, false);
        std::size_t reported_disjoint = 0;
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
            edge_j.push_back(0.0);
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
        tail_seconds += seconds_since(tail_start);

        const auto lsh_label = demo::components(n, edges);
        if (t == demo::LSH_THRESHOLD && ((k == demo::LSH_K && rows == demo::LSH_ROWS) ||
                                         (k == COMPARE_K && rows == COMPARE_ROWS))) {
          saved[k == demo::LSH_K ? 0 : 1] = reported_pairs{edges, edge_j, edge_estimate, lsh_label};
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
                                   std::to_string(k) + ", r = " + std::to_string(rows) + ", t = " + fixed(t, 1));
        }
        g.clusters = demo::compare_clusters(lsh_label, true_label[ti]);
        g.worst = demo::worst_merges(lsh_label, true_label[ti], edges, edge_j, demo::WORST_MERGES);
        for (const auto& pr : cross) {
          if (true_label[ti][pr.first] != true_label[ti][pr.second] || lsh_label[pr.first] != lsh_label[pr.second]) {
            ++g.cross_split;
          }
        }
      }
    }
    std::cout << "k = " << k << ": " << fixed(seconds_since(k_start), 1) << " s\n";
  }
  std::cout << "tails: " << fixed(tail_seconds, 3) << " s\n";

  // Verdicts.
  std::size_t pair_failed = 0;
  std::size_t cross_failed = 0;
  for (const auto& g : results) {
    pair_failed += g.pairs.pass ? 0 : 1;
    cross_failed += g.cross_split;
  }
  const bool prefix_pass = prefix_failed == 0;
  const bool cross_pass = cross_failed == 0;
  const bool overall = pair_failed == 0 && prefix_pass && cross_pass;

  // The MH-14 setting: k = LSH_K, LSH_BANDS x LSH_ROWS, t = LSH_THRESHOLD.
  const grid_result* ref = nullptr;
  for (const auto& g : results) {
    if (g.point.k == demo::LSH_K && g.point.rows == demo::LSH_ROWS && g.point.threshold == demo::LSH_THRESHOLD) {
      ref = &g;
    }
  }
  const auto t_ref = static_cast<std::size_t>(
      std::find(demo::FULL_THRESHOLDS.begin(), demo::FULL_THRESHOLDS.end(), demo::LSH_THRESHOLD) -
      demo::FULL_THRESHOLDS.begin());
  const auto band_label = [](std::size_t band) {
    return "[" + fixed(static_cast<double>(band) / demo::NUM_BINS, 1) + ", " +
           fixed(static_cast<double>(band + 1) / demo::NUM_BINS, 1) + (band + 1 == demo::NUM_BINS ? "]" : ")");
  };

  // TSV files.
  std::ostringstream meta;
  meta << "# ticket=MH-14b\n# smoke=" << (smoke ? 1 : 0) << "\n# indexed_posts=" << n << "\n# pairs_j_gt_0=" << n_pairs
       << "\n# family_alpha=" << general(demo::FULL_ALPHA) << "\n# test_alpha=" << general(test_alpha) << "\n";
  std::ostringstream bands_tsv;
  bands_tsv << tsv_licence() << meta.str()
            << "K\tR\tB\tT\tBandLo\tBandHi\tPairs\tVerified\tVerifiedRate\tExpectedVerified\tExpectedRate\n";
  for (const auto& g : results) {
    for (std::size_t band = 0; band < demo::NUM_BINS; ++band) {
      const auto& b = g.bands[band];
      const auto count = static_cast<double>(b.pairs);
      bands_tsv << g.point.k << "\t" << g.point.rows << "\t" << g.point.bands << "\t" << fixed(g.point.threshold, 1)
                << "\t" << fixed(static_cast<double>(band) / demo::NUM_BINS, 1) << "\t"
                << fixed(static_cast<double>(band + 1) / demo::NUM_BINS, 1) << "\t" << b.pairs << "\t" << b.verified
                << "\t" << (b.pairs == 0 ? "NA" : fixed(static_cast<double>(b.verified) / count, 6)) << "\t"
                << fixed(b.expected, 4) << "\t" << (b.pairs == 0 ? "NA" : fixed(b.expected / count, 6)) << "\n";
    }
  }

  // A random sample of the LSH clusters with at least SAMPLE_CLUSTER_MIN_SIZE posts at the MH-14 setting: the
  // clusters whose smallest post index has the smallest XXHash64 (seed SAMPLE_SEED).
  std::ostringstream sample_tsv;
  sample_tsv << tsv_licence() << meta.str() << "# threshold=" << fixed(demo::LSH_THRESHOLD, 1) << "\n"
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
          if (it != pair_index.end() && exact_j[it->second] >= demo::LSH_THRESHOLD &&
              verified_edges.count({members[a], members[b]}) == 0) {
            sample_tsv << s + 1 << "\tmissed\t" << post_name(members[a]) << "\t" << post_name(members[b]) << "\t-\t"
                       << fixed(exact_j[it->second], 4) << "\t-\n";
          }
        }
      }
    }
  }

  std::ostringstream pairs_tsv;
  pairs_tsv << tsv_licence() << meta.str()
            << "K\tR\tB\tT\tMinMatches\tTruePairs\tFalsePairs\tCandidates\tReported\tReportedTrue\tReportedFalse\t"
               "Recall\tPrecision\tExpectedTrue\tExpectedFalse\tPRecall\tPPrecision\tCheck\n";
  std::ostringstream clusters_tsv;
  clusters_tsv << tsv_licence() << meta.str()
               << "K\tR\tB\tT\tLshClusters\tTrueClusters\tLshPairs\tTruePairs\tBothPairs\tPrecision\tRecall\tMerges\t"
                  "Splits\tLargestLsh\tLargestTrue\tCrossPostsSplit\tWorstMerges\n";
  for (const auto& g : results) {
    const auto& c = g.pairs.counts;
    const std::string point = std::to_string(g.point.k) + "\t" + std::to_string(g.point.rows) + "\t" +
                              std::to_string(g.point.bands) + "\t" + fixed(g.point.threshold, 1) + "\t";
    pairs_tsv << point << g.min_matches << "\t" << c.similar << "\t" << g.pairs.false_pairs << "\t" << g.candidates
              << "\t" << c.verified << "\t" << c.both << "\t" << c.verified - c.both << "\t" << ratio(c.both, c.similar)
              << "\t" << ratio(c.both, c.verified) << "\t" << fixed(g.pairs.expected_true, 2) << "\t"
              << fixed(g.pairs.expected_false, 2) << "\t" << general(g.pairs.p_recall) << "\t"
              << general(g.pairs.p_precision) << "\t" << verdict(g.pairs.pass) << "\n";
    const auto& cm = g.clusters;
    std::string worst;
    for (const auto& w : g.worst) {
      worst += (worst.empty() ? "" : ";") + std::to_string(w.true_parts) + "x" + std::to_string(w.nodes) + ":" +
               posts[post_of[w.weakest.first]].group + "/" + posts[post_of[w.weakest.first]].id + "|" +
               posts[post_of[w.weakest.second]].group + "/" + posts[post_of[w.weakest.second]].id + "@" +
               fixed(w.weakest_j, 4);
    }
    clusters_tsv << point << cm.lsh_clusters << "\t" << cm.true_clusters << "\t" << cm.lsh_pairs << "\t"
                 << cm.true_pairs << "\t" << cm.both_pairs << "\t" << ratio(cm.both_pairs, cm.lsh_pairs) << "\t"
                 << ratio(cm.both_pairs, cm.true_pairs) << "\t" << cm.merges << "\t" << cm.splits << "\t"
                 << cm.largest_lsh << "\t" << cm.largest_true << "\t" << g.cross_split << "\t"
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
  md << "# Full-data precision and recall of the sketch and the LSH clusters on 20 Newsgroups"
     << (smoke ? " (smoke run)" : "") << "\n\n";
  md << "Written by `build/lsh_20news_full` (`FULL=1 ./run_demos.sh`). The ticket is MH-14b. The tables for every "
        "grid point are in `lsh_20news_full_pairs.tsv`, `lsh_20news_full_clusters.tsv` and "
        "`lsh_20news_full_estimates.tsv`. `plots/lsh_20news_full.py` draws the figures from them.\n\n";
  md << "## Definitions\n\n";
  md << "- **Data point:** one post. It is the set of its " << news20::SHINGLE_WORDS
     << "-word shingles: runs of " << news20::SHINGLE_WORDS
     << " consecutive lower-case words, after the header block of the post is removed. An indexed post has at "
        "least 1 shingle.\n";
  md << "- **Pair:** two different indexed posts, in any order.\n";
  md << "- **Exact J:** |A ∩ B| / |A ∪ B| of the two shingle sets. **Estimated J (Ĵ):** the share of the k slots "
        "whose values are equal.\n";
  md << "- **True pair at t:** a pair with exact J ≥ t.\n";
  md << "- **Candidate pair:** a pair whose band keys are equal in at least 1 band.\n";
  md << "- **Verified pair:** a candidate pair with Ĵ ≥ t. The TSV columns `Reported`, `ReportedTrue` and "
        "`ReportedFalse`, and the word \"reported\" in the figures, mean the verified pairs.\n";
  md << "- **Pair recall:** verified true pairs ÷ true pairs. **Pair precision:** verified true pairs ÷ verified "
        "pairs.\n";
  md << "- **True cluster:** a connected group of 2 or more posts that true pairs link. **LSH cluster:** a connected "
        "group of 2 or more posts that verified pairs link (`get_duplicate_clusters()`).\n";
  md << "- **Co-membership pair:** two posts in the same cluster. It includes posts that only a chain links, so "
        "their exact J can be below t.\n";
  md << "- **Cluster recall:** the co-membership pairs that are in the true clusters and in the LSH clusters ÷ the "
        "co-membership pairs of the true clusters. **Cluster precision:** the same count ÷ the co-membership pairs "
        "of the LSH clusters. These are not the same quantities as pair recall and pair precision, because a "
        "cluster counts every pair that a chain links.\n";
  md << "- **Merge:** an LSH cluster that joins 2 or more true clusters. Here a post in no true cluster counts as a "
        "true cluster of its own. **Split:** a true cluster that LSH divides into 2 or more parts.\n\n";
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
  md << "- posts: " << chosen.size() << (smoke ? " (the MH-8 sample)" : "") << "\n";
  md << "- indexed posts (at least one shingle of " << news20::SHINGLE_WORDS << " words): " << n << "\n";
  md << "- pairs of indexed posts: " << n * (n - 1) / 2 << "\n";
  md << "- pairs with exact J > 0 (stored in the exact-pair cache): " << n_pairs << "\n";
  md << "- grid: k ∈ {64, 128, 256, 512, 1024}, r ∈ {2, 4, 8, 16}, b = k / r, t ∈ {0.3, …, 0.9}: " << grid.size()
     << " points\n\n";
  md << "Truth: a true pair at t has exact J ≥ t, with J from `news20::jaccard` on the MH-8 shingle sets. The true "
        "clusters at t are the connected components of the true pairs. Verified pairs: the candidates (equal band keys "
        "in at least one band) with Ĵ ≥ t. The LSH clusters are `get_duplicate_clusters()`. The program asserts, at "
        "each grid point, that the components of the verified pairs equal them.\n\n";

  md << "## Prefix assert: " << verdict(prefix_pass) << "\n\n";
  md << "Each sketch is built directly at its k. For each k < 1024 and each indexed post, the k signature must equal "
        "the first k slots of the k = 1024 signature.\n\n";
  md << "- signatures checked: " << prefix_checked << "\n- signatures that differ: " << prefix_failed << "\n\n";

  md << "## Cross-posts: " << verdict(cross_pass) << "\n\n";
  md << "Each pair of indexed posts that share a `Message-ID` must be in one true cluster and in one LSH cluster, at "
        "every grid point.\n\n";
  md << "- cross-post pairs: " << cross.size() << "\n- (pair, grid point) cases not in one cluster: " << cross_failed
     << "\n\n";

  md << "## Pair check: " << (pair_failed == 0 ? "PASS" : "FAIL") << " (" << grid.size() - pair_failed << " of "
     << grid.size() << " grid points pass)\n\n";
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
     << ". They are not: pairs that share a post use the same signature, so their outcomes are positively "
        "correlated, the counts spread more than the Poisson-binomial allows, and the p-values are too small. So "
        "this bound is not exact. The margin here is wide: the smallest p-value is in "
        "`lsh_20news_full_pairs.tsv` and the figure below. MH-14 checks the S-curve on independent synthetic pairs "
        "for this reason.\n\n";
  md << "![Measured counts against the theory, and all p-values](lsh_20news_full_theory.svg)\n\n";
  md << "How to read it: on the left, each dot is one count at one grid point. A dot on the line means that the "
        "measured count equals the theory. On the right, the p-values of all tests are sorted. A test fails only "
        "below the red line.\n\n";
  if (pair_failed > 0) {
    md << "| k | r | t | recall p | precision p |\n|---:|---:|---:|---:|---:|\n";
    for (const auto& g : results) {
      if (!g.pairs.pass) {
        md << "| " << g.point.k << " | " << g.point.rows << " | " << fixed(g.point.threshold, 1) << " | "
           << general(g.pairs.p_recall) << " | " << general(g.pairs.p_precision) << " |\n";
      }
    }
    md << "\n";
  }

  md << "## Recommended k\n\n";
  md << "For each t: the smallest k with a (b, r) whose pair recall and pair precision are both ≥ "
     << fixed(demo::FULL_TARGET, 2)
     << " and whose pair check passes. The best (b, r) at that k has the largest min(recall, precision), ties by the "
        "smaller r.\n\n";
  md << "| t | k | b × r | recall | precision |\n|---:|---:|---|---:|---:|\n";
  for (std::size_t ti = 0; ti < n_t; ++ti) {
    const grid_result* best = nullptr;
    for (std::size_t ki = 0; ki < demo::FULL_K_VALUES.size() && best == nullptr; ++ki) {
      for (std::size_t ri = 0; ri < n_rows; ++ri) {
        const auto& g = results[(ki * n_rows + ri) * n_t + ti];
        const auto score = [](const grid_result& x) { return std::min(recall_of(x.pairs), precision_of(x.pairs)); };
        if (meets_target(g) && (best == nullptr || score(g) > score(*best))) {
          best = &g;
        }
      }
    }
    md << "| " << fixed(demo::FULL_THRESHOLDS[ti], 1) << " | ";
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
  md << "![Recall and precision at every grid point](lsh_20news_full_heatmap.svg)\n\n";
  md << "How to read it: each row is one (k, b × r), and each column is one t. Bright cells are near 1, and bold "
        "values are at least 0.95. Recall rises and precision falls as r gets smaller.\n\n";
  md << "![Precision against recall for each k](lsh_20news_full_pr.svg)\n\n";
  md << "How to read it: one panel for each k, and one curve for each r. Each curve runs from t = 0.3 to t = 0.9. "
        "The dotted lines mark 0.95, so a point in the top-right corner meets the target of a recommended k.\n\n";
  md << "Each cell is recall / precision. A FAIL marks a failed pair check.\n\n| k | b × r |";
  for (const double t : demo::FULL_THRESHOLDS) {
    md << " t = " << fixed(t, 1) << " |";
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
        "mean of P(verified | J) over the pairs of the band. All grid points are in `lsh_20news_full_bands.tsv`.\n\n";
  if (ref != nullptr) {
    md << "At k = " << demo::LSH_K << ", " << demo::LSH_BANDS << " × " << demo::LSH_ROWS << ", t = "
       << fixed(demo::LSH_THRESHOLD, 1) << ":\n\n";
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
  md << "![Verified rate for each J band](lsh_20news_full_bands.svg)\n\n";
  md << "How to read it: the dots are the measured verified rate of each band, and the line is the expected rate. "
        "The dotted line marks t. Right of it, the rate is the recall for pairs of that J; left of it, the rate is "
        "the false-verify rate. The right panel shows the same rates on a log scale, so small rates are visible "
        "(a band with no verified pair is not drawn there).\n\n";
  md << "## Estimate error\n\n";
  md << "On the stored pairs with exact J ≥ " << fixed(demo::ESTIMATE_MIN_J, 1)
     << ". The ratio is the RMSE divided by the RMS of √(J (1 − J) / k), so 1 is the theory.\n\n";
  md << "![Estimate error against k](lsh_20news_full_estimates.svg)\n\n";
  md << "How to read it: on the left, the measured RMSE (blue) lies on the theory (orange), and it halves when k "
        "grows 4 times. The bias (green) is much smaller than the RMSE. On the right, the ratio is near 1 at every "
        "k.\n\n";
  md << "| k | pairs | mean error | RMSE | RMS theory SD | ratio |\n|---:|---:|---:|---:|---:|---:|\n";
  for (const auto& e : estimates) {
    md << "| " << e.k << " | " << e.pairs << " | " << fixed(e.mean_error, 6) << " | " << fixed(e.rmse, 6) << " | "
       << fixed(e.rms_sd, 6) << " | " << (e.rms_sd > 0.0 ? fixed(e.rmse / e.rms_sd, 4) : "NA") << " |\n";
  }
  md << "\n";

  md << "## Clusters at k = " << demo::LSH_K << ", " << demo::LSH_BANDS << " × " << demo::LSH_ROWS << ", t = "
     << fixed(demo::LSH_THRESHOLD, 1) << " (the MH-14 setting)\n\n";
  md << "Co-membership: a pair of posts is in one cluster. A post alone counts as a component of its own, so a merge "
        "can join a true cluster and a post that is in no true cluster. All grid points are in "
        "`lsh_20news_full_clusters.tsv`.\n\n";
  md << "![Cluster precision against recall for each k](lsh_20news_full_clusters.svg)\n\n";
  md << "How to read it: as the pair figure, but for pairs of posts in one cluster of `get_duplicate_clusters()` "
        "against pairs in one true cluster.\n\n";
  if (ref != nullptr) {
    const auto& cm = ref->clusters;
    md << "- LSH clusters: " << cm.lsh_clusters << "; true clusters: " << cm.true_clusters << "\n";
    md << "- co-membership precision: " << ratio(cm.both_pairs, cm.lsh_pairs) << " (" << cm.both_pairs << " of "
       << cm.lsh_pairs << " pairs)\n";
    md << "- co-membership recall: " << ratio(cm.both_pairs, cm.true_pairs) << " (" << cm.both_pairs << " of "
       << cm.true_pairs << " pairs)\n";
    md << "- merges (LSH clusters that join 2 or more true components): " << cm.merges << "\n";
    md << "- splits (true clusters that LSH splits): " << cm.splits << "\n";
    md << "- largest LSH cluster: " << cm.largest_lsh << " posts; largest true cluster: " << cm.largest_true
       << " posts\n\n";
    md << "The " << demo::WORST_MERGES
       << " worst merges (most true components, then most posts), each with its weakest link: the verified pair of the "
          "cluster with the lowest exact J.\n\n";
    md << "| true components | posts | weakest link | exact J | 3 shared shingles |\n|---:|---:|---|---:|---|\n";
    for (const auto& w : ref->worst) {
      md << "| " << w.true_parts << " | " << w.nodes << " | " << name(w.weakest.first) << ", " << name(w.weakest.second)
         << " | " << fixed(w.weakest_j, 4) << " | " << shared_shingles(sets[w.weakest.first], sets[w.weakest.second])
         << " |\n";
    }
    md << "\n";
    md << "![The worst merges as graphs](lsh_20news_full_merges.svg)\n\n";
    md << "How to read it: each column is one merge of the table above, in the same order. Each dot is a post, "
          "coloured by its true cluster at t = "
       << fixed(demo::LSH_THRESHOLD, 1)
       << ". Each line is a verified pair, labelled with its exact J. A grey line joins posts of one true cluster. "
          "A red line has J < t, so it is a false link that joins 2 true clusters, and the thickest red line is the "
          "weakest link. The top row is the MH-14 setting (k = "
       << demo::LSH_K << ", " << demo::LSH_BANDS << " × " << demo::LSH_ROWS << "). The bottom row shows the same posts at k = "
       << COMPARE_K << ", " << COMPARE_K / COMPARE_ROWS << " × " << COMPARE_ROWS
       << ", with only the pairs among these posts. The data is in `lsh_20news_full_merges.tsv`.\n\n";
    md << "### A random sample of the clusters\n\n";
    md << sample_drawn << " of the " << sample_eligible << " LSH clusters with at least " << demo::SAMPLE_CLUSTER_MIN_SIZE
       << " posts at this setting, chosen by the smallest XXHash64 (seed " << demo::SAMPLE_SEED
       << ") of the smallest post index of each cluster. " << sample_merges << " of them contain a merge.\n\n";
    md << "![A random sample of the clusters as graphs](lsh_20news_full_sample_clusters.svg)\n\n";
    md << "How to read it: each panel is one LSH cluster. Each dot is a post, coloured by its true cluster at t = "
       << fixed(demo::LSH_THRESHOLD, 1)
       << ", and a grey dot is in no true cluster. A solid line is a verified pair, labelled with its exact J, and "
          "it is red when J < t. A dashed line is a true pair (J ≥ t) between drawn posts that LSH did not verify; "
          "the chain still puts both posts in the cluster. The data is in `lsh_20news_full_sample_clusters.tsv`.\n\n";
  }

  md << "## The " << TOP_PAIRS << " true pairs with the highest J\n\n";
  md << "Ties by post index. The shingles show whether quoted text or boilerplate makes the pairs similar.\n\n";
  md << "| posts | exact J | 3 shared shingles |\n|---|---:|---|\n";
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

  // The merge figure data: the posts of each worst merge at the MH-14 setting, and the reported pairs among them.
  std::ostringstream merges_tsv;
  merges_tsv << tsv_licence() << meta.str() << "# threshold=" << fixed(demo::LSH_THRESHOLD, 1) << "\n"
             << "Merge\tKind\tSetting\tPost\tOther\tTrueCluster\tExactJ\tEstimatedJ\n";
  if (ref != nullptr) {
    const std::array<std::string, 2> setting{
        "k" + std::to_string(demo::LSH_K) + "_b" + std::to_string(demo::LSH_BANDS) + "_r" + std::to_string(demo::LSH_ROWS),
        "k" + std::to_string(COMPARE_K) + "_b" + std::to_string(COMPARE_K / COMPARE_ROWS) + "_r" + std::to_string(COMPARE_ROWS)};
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

  write_file(out_dir / "lsh_20news_full_merges.tsv", merges_tsv.str());
  write_file(out_dir / "lsh_20news_full_bands.tsv", bands_tsv.str());
  write_file(out_dir / "lsh_20news_full_sample_clusters.tsv", sample_tsv.str());
  write_file(out_dir / "lsh_20news_full_pairs.tsv", pairs_tsv.str());
  write_file(out_dir / "lsh_20news_full_clusters.tsv", clusters_tsv.str());
  write_file(out_dir / "lsh_20news_full_estimates.tsv", estimates_tsv.str());
  write_file(out_dir / "lsh_20news_full.md", md.str());

  std::cout << "prefix assert: " << verdict(prefix_pass) << " (" << prefix_failed << " of " << prefix_checked
            << " differ)\n";
  std::cout << "cross-posts: " << verdict(cross_pass) << "\n";
  std::cout << "pair check: " << grid.size() - pair_failed << " of " << grid.size() << " pass\n";
  for (const auto& g : results) {
    if (!g.pairs.pass) {
      std::cout << "  FAIL k = " << g.point.k << ", r = " << g.point.rows << ", t = " << fixed(g.point.threshold, 1)
                << ": recall p " << general(g.pairs.p_recall) << ", precision p " << general(g.pairs.p_precision)
                << "\n";
    }
  }
  std::cout << "total: " << fixed(seconds_since(start), 1) << " s\n";
  std::cout << "Overall: " << verdict(overall) << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const bool smoke = !args.empty() && args[0] == "--smoke";
  if (args.size() != (smoke ? 3U : 2U)) {
    std::cerr << "usage: " << argv[0] << " [--smoke] <20_newsgroups directory> <results directory>\n";
    return 2;
  }
  try {
    return run(smoke, args[smoke ? 1 : 0], args[smoke ? 2 : 1]);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}

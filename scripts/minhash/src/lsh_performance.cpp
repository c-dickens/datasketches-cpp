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

// Profile lsh_performance (MH-20): find all pairs with J >= t among N documents, with three
// methods. minhash_lsh adds the MinHash sketches to the LSH index and clusters them.
// minhash_brute and theta_brute estimate J on all N (N - 1) / 2 pairs. The data has planted
// groups of near-duplicates (lsh_documents), so every true pair is known. Every result goes into
// a sink, which is printed to stderr at the end, so that the compiler cannot remove the timed work.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "characterise/common.hpp"
#include "characterise/profiles.hpp"
#include "minhash_lsh.hpp"
#include "minhash_sketch.hpp"
#include "theta_jaccard_similarity.hpp"
#include "theta_sketch.hpp"

namespace characterise {

namespace {

using steady = std::chrono::steady_clock;
using alloc = counting_allocator<uint64_t>;
using sketch_type = datasketches::minhash_sketch_alloc<alloc>;
using index_type = datasketches::minhash_lsh_index_alloc<alloc>;

constexpr uint16_t K = 128;
constexpr uint8_t THETA_LG_K = 7;
constexpr double THRESHOLD = 0.5;
constexpr double PLANTED_FRACTION = 0.1;  // lsh_documents puts n / 10 ids in planted groups
constexpr uint64_t QUERY_SEED = 1;
constexpr std::pair<uint16_t, uint16_t> BANDS[] = {{32, 4}, {16, 8}};
constexpr unsigned PPO = 2;
constexpr uint64_t MIN_N = 1ULL << 8;
constexpr uint64_t FULL_LSH_MAX_N = 1ULL << 18;
constexpr uint64_t FULL_MINHASH_BRUTE_MAX_N = 1ULL << 14;
constexpr uint64_t FULL_THETA_BRUTE_MAX_N = 1ULL << 12;
constexpr uint64_t SMOKE_LSH_MAX_N = 1ULL << 12;
constexpr uint64_t SMOKE_MINHASH_BRUTE_MAX_N = 1ULL << 11;
constexpr uint64_t SMOKE_THETA_BRUTE_MAX_N = 1ULL << 10;
constexpr std::size_t FULL_QUERIES = 500;
constexpr std::size_t SMOKE_QUERIES = 100;
constexpr unsigned FULL_REPEATS = 3;
constexpr unsigned SMOKE_REPEATS = 1;

double seconds_since(steady::time_point start) {
  return std::chrono::duration<double>(steady::now() - start).count();
}

// The times of one (method, b, r, phase), one for each repeat, in the order of the first repeat.
struct timings {
  std::vector<std::string> keys;
  std::map<std::string, std::vector<double>> seconds;

  void add(const std::string& key, double s) {
    auto& v = seconds[key];
    if (v.empty()) {
      keys.push_back(key);
    }
    v.push_back(s);
  }
};

std::string time_key(const std::string& method, unsigned b, unsigned r, uint64_t n, const std::string& phase) {
  return method + "\t" + std::to_string(b) + "\t" + std::to_string(r) + "\t" + std::to_string(n) + "\t" + phase;
}

struct pair_counts {
  uint64_t reported = 0;
  uint64_t true_positives = 0;
};

bool same_group(const std::vector<lsh_document>& docs, uint64_t a, uint64_t b) {
  return docs[a].group >= 0 && docs[a].group == docs[b].group;
}

void write_quality(std::ostream& out, const std::string& method, unsigned b, unsigned r, uint64_t n,
                   uint64_t true_pairs, const pair_counts& pairs, uint64_t candidate_pairs,
                   const std::string& largest_bucket, double bytes_per_doc) {
  out << method << "\t" << b << "\t" << r << "\t" << n << "\t" << true_pairs << "\t" << pairs.reported << "\t"
      << pairs.true_positives << "\t"
      << static_cast<double>(pairs.true_positives) / static_cast<double>(true_pairs) << "\t";
  if (pairs.reported == 0) {
    out << "NA";
  } else {
    out << static_cast<double>(pairs.true_positives) / static_cast<double>(pairs.reported);
  }
  out << "\t" << candidate_pairs << "\t" << largest_bucket << "\t" << bytes_per_doc << "\n";
}

// The pairs in the clusters of the index, and those in one planted group.
pair_counts cluster_pairs(const index_type::vector_of_clusters& clusters, const std::vector<lsh_document>& docs) {
  pair_counts counts;
  for (const auto& cluster : clusters) {
    const uint64_t c = cluster.size();
    counts.reported += c * (c - 1) / 2;
    std::map<int64_t, uint64_t> per_group;
    for (const uint64_t id : cluster) {
      if (docs[id].group >= 0) {
        ++per_group[docs[id].group];
      }
    }
    for (const auto& g : per_group) {
      counts.true_positives += g.second * (g.second - 1) / 2;
    }
  }
  return counts;
}

// CandidatePairs and LargestBucket of the bands of all sketches. The map is only counted, so
// its iteration order does not change the result.
std::pair<uint64_t, uint64_t> bucket_stats(const std::vector<sketch_type>& sketches, uint16_t b) {
  std::vector<std::unordered_map<uint64_t, uint64_t>> buckets(b);
  for (const auto& s : sketches) {
    const auto keys = index_type::band_keys(s, b);
    for (uint16_t band = 0; band < b; ++band) {
      ++buckets[band][keys[band]];
    }
  }
  uint64_t candidate_pairs = 0;
  uint64_t largest = 0;
  for (const auto& band : buckets) {
    for (const auto& bucket : band) {
      candidate_pairs += bucket.second * (bucket.second - 1) / 2;
      largest = std::max(largest, bucket.second);
    }
  }
  return {candidate_pairs, largest};
}

// All pairs i < j with jaccard(i, j) >= t, and those in one planted group. The group check runs
// only for the pairs that pass, so it adds almost nothing to the timed work, and the quality
// numbers can come from the timed run.
template<typename Sketch, typename Jaccard>
pair_counts brute_pairs(const std::vector<Sketch>& sketches, Jaccard jaccard, const std::vector<lsh_document>& docs) {
  pair_counts counts;
  for (uint64_t i = 0; i < sketches.size(); ++i) {
    for (uint64_t j = i + 1; j < sketches.size(); ++j) {
      if (jaccard(sketches[i], sketches[j]) >= THRESHOLD) {
        ++counts.reported;
        if (same_group(docs, i, j)) {
          ++counts.true_positives;
        }
      }
    }
  }
  return counts;
}

// The matches of the queries: the candidates e != q with jaccard(q, e) >= t.
uint64_t run_queries(const index_type& index, const std::vector<sketch_type>& sketches,
                     const std::vector<uint64_t>& queries) {
  uint64_t matches = 0;
  for (const uint64_t q : queries) {
    for (const uint64_t e : index.query_candidates(sketches[q])) {
      if (e != q && sketches[q].jaccard(sketches[e]) >= THRESHOLD) {
        ++matches;
      }
    }
  }
  return matches;
}

struct grid {
  std::vector<uint64_t> ns;
  uint64_t minhash_brute_max_n;
  uint64_t theta_brute_max_n;
  std::size_t queries;
  unsigned repeats;
};

void run_n(uint64_t n, const grid& g, timings& times, std::ostream& quality_out, double& sink) {
  const auto docs = lsh_documents(n);
  uint64_t true_pairs = 0;
  std::vector<uint64_t> planted_ids;
  std::vector<uint64_t> unrelated_ids;
  for (const auto& d : docs) {
    if (d.group < 0) {
      unrelated_ids.push_back(d.id);
    } else {
      // Each member adds its pairs with the earlier members of its group.
      for (uint64_t e = d.id; e > 0 && docs[e - 1].group == d.group; --e) {
        ++true_pairs;
      }
      planted_ids.push_back(d.id);
    }
  }
  const auto planted_queries = smallest_hash_ids(planted_ids, g.queries, QUERY_SEED);
  const auto unrelated_queries = smallest_hash_ids(unrelated_ids, g.queries, QUERY_SEED);

  for (unsigned repeat = 0; repeat < g.repeats; ++repeat) {
    const bool first = repeat == 0;
    auto start = steady::now();
    std::vector<sketch_type> sketches;
    sketches.reserve(n);
    for (uint64_t d = 0; d < n; ++d) {
      auto s = sketch_type::builder().set_k(K).build();
      lsh_document_items(docs, d, [&s](uint64_t item) { s.update(item); });
      sketches.push_back(std::move(s));
    }
    times.add(time_key("minhash", 0, 0, n, "sketch"), seconds_since(start));
    sink += static_cast<double>(sketches.back().get_signature()[0]);

    for (const auto& br : BANDS) {
      const int64_t bytes_before = counted_bytes();
      index_type index(br.first, THRESHOLD);
      start = steady::now();
      for (uint64_t d = 0; d < n; ++d) {
        index.add(d, sketches[d]);
      }
      times.add(time_key("minhash_lsh", br.first, br.second, n, "add"), seconds_since(start));
      const int64_t index_bytes = counted_bytes() - bytes_before;
      sink += static_cast<double>(index.get_num_documents());

      start = steady::now();
      const auto clusters = index.get_duplicate_clusters();
      times.add(time_key("minhash_lsh", br.first, br.second, n, "cluster"), seconds_since(start));
      sink += static_cast<double>(clusters.size());

      start = steady::now();
      sink += static_cast<double>(run_queries(index, sketches, planted_queries));
      times.add(time_key("minhash_lsh", br.first, br.second, n, "query_planted"), seconds_since(start));
      start = steady::now();
      sink += static_cast<double>(run_queries(index, sketches, unrelated_queries));
      times.add(time_key("minhash_lsh", br.first, br.second, n, "query_unrelated"), seconds_since(start));

      if (first) {
        const auto stats = bucket_stats(sketches, br.first);
        write_quality(quality_out, "minhash_lsh", br.first, br.second, n, true_pairs, cluster_pairs(clusters, docs),
                      stats.first, std::to_string(stats.second),
                      static_cast<double>(index_bytes) / static_cast<double>(n));
      }
    }

    const auto minhash_jaccard = [](const sketch_type& a, const sketch_type& b) { return a.jaccard(b); };
    if (n <= g.minhash_brute_max_n) {
      start = steady::now();
      const auto pairs = brute_pairs(sketches, minhash_jaccard, docs);
      times.add(time_key("minhash_brute", 0, 0, n, "brute"), seconds_since(start));
      sink += static_cast<double>(pairs.reported);
      if (first) {
        write_quality(quality_out, "minhash_brute", 0, 0, n, true_pairs, pairs,
                      n * (n - 1) / 2, "NA", 8.0 * K);
      }
    }
    sketches.clear();
    sketches.shrink_to_fit();

    if (n <= g.theta_brute_max_n) {
      start = steady::now();
      std::vector<datasketches::compact_theta_sketch> thetas;
      thetas.reserve(n);
      for (uint64_t d = 0; d < n; ++d) {
        auto s = datasketches::update_theta_sketch::builder().set_lg_k(THETA_LG_K).build();
        lsh_document_items(docs, d, [&s](uint64_t item) { s.update(item); });
        thetas.push_back(s.compact());
      }
      times.add(time_key("theta", 0, 0, n, "sketch"), seconds_since(start));
      sink += static_cast<double>(thetas.back().get_num_retained());

      const auto theta_jaccard = [](const datasketches::compact_theta_sketch& a,
                                    const datasketches::compact_theta_sketch& b) {
        return datasketches::theta_jaccard_similarity::jaccard(a, b)[1];
      };
      start = steady::now();
      const auto pairs = brute_pairs(thetas, theta_jaccard, docs);
      times.add(time_key("theta_brute", 0, 0, n, "brute"), seconds_since(start));
      sink += static_cast<double>(pairs.reported);
      if (first) {
        double bytes = 0.0;
        for (const auto& s : thetas) {
          bytes += static_cast<double>(s.get_serialized_size_bytes());
        }
        write_quality(quality_out, "theta_brute", 0, 0, n, true_pairs, pairs,
                      n * (n - 1) / 2, "NA", bytes / static_cast<double>(n));
      }
    }
  }
}

void write_header(std::ostream& out, const char* profile, const grid& g) {
  write_metadata(out, profile);
  out << "# cpu=" << cpu_model() << "\n";
  out << "# k=" << K << "\n";
  out << "# threshold=" << THRESHOLD << "\n";
  out << "# repeats=" << g.repeats << "\n";
  out << "# planted_fraction=" << PLANTED_FRACTION << "\n";
  // A class with fewer ids than this has one query for each id.
  out << "# queries_per_class=" << g.queries << "\n";
}

}  // namespace

void lsh_performance(std::ostream& time_out, std::ostream& quality_out, bool smoke) {
  const uint64_t lsh_max_n = smoke ? SMOKE_LSH_MAX_N : FULL_LSH_MAX_N;
  const grid g{pwr_2_law_points(MIN_N, lsh_max_n, PPO), smoke ? SMOKE_MINHASH_BRUTE_MAX_N : FULL_MINHASH_BRUTE_MAX_N,
               smoke ? SMOKE_THETA_BRUTE_MAX_N : FULL_THETA_BRUTE_MAX_N, smoke ? SMOKE_QUERIES : FULL_QUERIES,
               smoke ? SMOKE_REPEATS : FULL_REPEATS};
  const char* profile = smoke ? "minhash-lsh-performance-smoke" : "minhash-lsh-performance";

  time_out << std::setprecision(10);
  quality_out << std::setprecision(10);
  write_header(time_out, profile, g);
  write_header(quality_out, profile, g);
  time_out << "Method\tB\tR\tN\tPhase\tRepeats\tMedianSec\tMinSec\n";
  quality_out << "Method\tB\tR\tN\tTruePairs\tReportedPairs\tTruePositives\tRecall\tPrecision\tCandidatePairs"
                 "\tLargestBucket\tBytesPerDoc\n";

  double sink = 0.0;
  const auto start = steady::now();
  for (const uint64_t n : g.ns) {
    timings times;
    run_n(n, g, times, quality_out, sink);
    for (const auto& key : times.keys) {
      auto v = times.seconds[key];
      std::sort(v.begin(), v.end());
      // The median of an even count is the mean of the two middle values.
      const double median = v.size() % 2 == 1 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2.0;
      time_out << key << "\t" << v.size() << "\t" << median << "\t" << v.front() << "\n";
    }
    std::cerr << "lsh_performance: N = " << n << " done, " << std::fixed << std::setprecision(1)
              << seconds_since(start) << " s elapsed\n"
              << std::defaultfloat;
  }
  std::cerr << "lsh_performance: sink = " << sink << "\n";
}

}  // namespace characterise

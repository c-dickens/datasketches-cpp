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

/**
 * Near-duplicate detection on the 20 Newsgroups dataset using MinHash + LSH.
 *
 * Usage:
 *   minhash_newsgroups <path-to-20_newsgroups/>
 *
 * The dataset has ~19,997 Usenet posts across 20 newsgroups. Cross-posted
 * messages appear in multiple groups as near-identical files (headers differ,
 * body is the same). This program finds those near-duplicates.
 *
 * Download:
 *   curl -O http://qwone.com/~jason/20Newsgroups/20news-19997.tar.gz
 *   tar xzf 20news-19997.tar.gz
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <vector>

#include "minhash_lsh.hpp"

using namespace datasketches;

// Monotonic clock in milliseconds.
static double now_ms() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// Strip RFC 822 headers: everything before the first blank line.
static std::string extract_body(const std::string& raw) {
  auto pos = raw.find("\n\n");
  if (pos == std::string::npos) return raw;
  return raw.substr(pos + 2);
}

static std::string read_file(const std::string& path) {
  std::ifstream ifs(path.c_str(), std::ios::binary);
  if (!ifs) return std::string();
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// Recursively collect regular files under a directory.
static void collect_files(const std::string& dir, std::vector<std::string>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* entry;
  while ((entry = readdir(d)) != nullptr) {
    if (entry->d_name[0] == '.') continue; // skip . and ..
    std::string full = dir + "/" + entry->d_name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode))
      collect_files(full, out);
    else if (S_ISREG(st.st_mode))
      out.push_back(full);
  }
  closedir(d);
}

// Build a MinHash sketch from text using character 5-grams.
static minhash_sketch sketch_text(const std::string& text, uint16_t k) {
  auto sketch = minhash_sketch::builder().set_k(k).build();
  if (text.size() >= 5) {
    for (size_t i = 0; i + 5 <= text.size(); ++i)
      sketch.update(text.substr(i, 5));
  }
  return sketch;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <path-to-20_newsgroups/>\n";
    return 1;
  }

  const std::string root(argv[1]);
  // Strip trailing slash for clean relative paths.
  const std::string root_prefix = (root.back() == '/') ? root : root + "/";

  // LSH parameters.
  const uint16_t k = 128;          // signature slots
  const uint16_t num_bands = 32;   // 32 bands x 4 rows
  const uint16_t rows = k / num_bands;
  const double threshold = 0.8;    // high threshold: genuine near-duplicates

  std::cout << "=== MinHash Near-Duplicate Detection: 20 Newsgroups ===\n\n";
  std::cout << "Configuration: k=" << k << ", " << num_bands << " bands x " << rows << " rows"
            << ", threshold=" << std::fixed << std::setprecision(2) << threshold << "\n";
  std::cout << "S-curve midpoint (P=0.5): J ~= " << std::setprecision(2)
            << std::pow(1.0 / num_bands, 1.0 / rows) << "\n\n";

  // -----------------------------------------------------------------------
  // Phase 1: Read files, build sketches.
  // -----------------------------------------------------------------------
  struct Doc {
    uint64_t id;
    std::string path;     // relative path like "alt.atheism/49960"
    std::string body;
  };
  std::vector<Doc> docs;
  std::vector<minhash_sketch> sketches;

  std::cout << "Reading files from " << root << " ...\n";
  double t_start = now_ms();

  std::vector<std::string> files;
  collect_files(root, files);
  std::sort(files.begin(), files.end()); // deterministic order

  uint64_t next_id = 0;
  uint64_t skipped_short = 0;
  for (const auto& filepath : files) {
    std::string raw = read_file(filepath);
    std::string body = extract_body(raw);
    if (body.size() < 50) { ++skipped_short; continue; }

    // Make a relative path for display.
    std::string rel = filepath;
    if (rel.substr(0, root_prefix.size()) == root_prefix)
      rel = rel.substr(root_prefix.size());

    auto sketch = sketch_text(body, k);
    Doc d;
    d.id = next_id;
    d.path = rel;
    d.body = body;
    docs.push_back(d);
    sketches.push_back(std::move(sketch));
    ++next_id;
  }

  double t_sketch = now_ms();
  double sketch_ms = t_sketch - t_start;

  std::cout << "  " << docs.size() << " documents sketched (" << skipped_short
            << " skipped as too short)\n";
  std::cout << "  Sketch time: " << std::fixed << std::setprecision(0) << sketch_ms << " ms\n\n";

  // -----------------------------------------------------------------------
  // Phase 2: Build LSH index.
  // -----------------------------------------------------------------------
  std::cout << "Building LSH index ...\n";
  double t_index_start = now_ms();

  minhash_lsh_index index(num_bands, threshold);
  for (size_t i = 0; i < docs.size(); ++i)
    index.add(docs[i].id, sketches[i]);

  double index_ms = now_ms() - t_index_start;
  std::cout << "  " << index.get_num_documents() << " documents indexed\n";
  std::cout << "  Index time: " << std::fixed << std::setprecision(0) << index_ms << " ms\n\n";

  // -----------------------------------------------------------------------
  // Phase 3: Find duplicate clusters.
  // -----------------------------------------------------------------------
  std::cout << "Finding duplicate clusters ...\n";
  double t_cluster_start = now_ms();

  const auto clusters = index.get_duplicate_clusters();

  double cluster_ms = now_ms() - t_cluster_start;

  size_t total_in_clusters = 0;
  for (size_t ci = 0; ci < clusters.size(); ++ci)
    total_in_clusters += clusters[ci].size();

  std::cout << "  " << clusters.size() << " clusters containing "
            << total_in_clusters << " documents\n";
  std::cout << "  Cluster time: " << std::fixed << std::setprecision(0) << cluster_ms << " ms\n\n";

  // -----------------------------------------------------------------------
  // Phase 4: Analyse clusters.
  // -----------------------------------------------------------------------

  // --- Cluster size distribution ---
  std::map<size_t, size_t> size_hist; // cluster size -> count
  for (size_t ci = 0; ci < clusters.size(); ++ci)
    ++size_hist[clusters[ci].size()];

  std::cout << "--- Cluster size distribution ---\n";
  for (const auto& entry : size_hist)
    std::cout << "  " << entry.first << " docs: " << entry.second << " clusters\n";
  std::cout << "\n";

  // --- Jaccard distribution ---
  size_t exact_dups = 0;     // J == 1.0
  size_t high_dups = 0;      // 0.9 <= J < 1.0
  size_t moderate_dups = 0;  // threshold <= J < 0.9
  size_t total_pairs_checked = 0;
  for (size_t ci = 0; ci < clusters.size(); ++ci) {
    const auto& cluster = clusters[ci];
    for (size_t i = 0; i < cluster.size(); ++i) {
      for (size_t j = i + 1; j < cluster.size(); ++j) {
        double jval = sketches[cluster[i]].jaccard(sketches[cluster[j]]);
        if (jval >= 1.0 - 1e-9) ++exact_dups;
        else if (jval >= 0.9) ++high_dups;
        else ++moderate_dups;
        ++total_pairs_checked;
      }
    }
  }

  std::cout << "--- Jaccard distribution (within clusters) ---\n";
  std::cout << "  Exact duplicates (J=1.0):   " << exact_dups << " pairs\n";
  std::cout << "  High similarity (0.9<=J<1): " << high_dups << " pairs\n";
  std::cout << "  Moderate (threshold<=J<0.9):" << moderate_dups << " pairs\n";
  std::cout << "  Total intra-cluster pairs:  " << total_pairs_checked << "\n\n";

  // --- Cross-group vs same-group ---
  // Extract the newsgroup name (directory component) from each doc path.
  size_t cross_group = 0, same_group = 0;
  for (size_t ci = 0; ci < clusters.size(); ++ci) {
    const auto& cluster = clusters[ci];
    // Get the newsgroup for each doc in the cluster.
    std::vector<std::string> groups;
    for (size_t i = 0; i < cluster.size(); ++i) {
      const std::string& p = docs[cluster[i]].path;
      size_t slash = p.find('/');
      groups.push_back(slash != std::string::npos ? p.substr(0, slash) : p);
    }
    // Check if all docs are in the same group or span multiple.
    bool all_same = true;
    for (size_t i = 1; i < groups.size(); ++i) {
      if (groups[i] != groups[0]) { all_same = false; break; }
    }
    if (all_same) ++same_group;
    else ++cross_group;
  }

  std::cout << "--- Duplicate type ---\n";
  std::cout << "  Cross-posted (different newsgroups): " << cross_group << " clusters\n";
  std::cout << "  Same-group duplicates:               " << same_group << " clusters\n\n";

  // --- Show a few example clusters from each type ---
  std::cout << "--- Example cross-posts ---\n\n";
  int shown = 0;
  for (size_t ci = 0; ci < clusters.size() && shown < 5; ++ci) {
    const auto& cluster = clusters[ci];
    // Check if this is a cross-post (members in different groups).
    std::string first_group;
    {
      size_t slash = docs[cluster[0]].path.find('/');
      first_group = (slash != std::string::npos) ? docs[cluster[0]].path.substr(0, slash) : "";
    }
    bool is_cross = false;
    for (size_t i = 1; i < cluster.size(); ++i) {
      size_t slash = docs[cluster[i]].path.find('/');
      std::string g = (slash != std::string::npos) ? docs[cluster[i]].path.substr(0, slash) : "";
      if (g != first_group) { is_cross = true; break; }
    }
    if (!is_cross) continue;

    std::cout << "Cluster " << ci + 1 << " (" << cluster.size() << " docs):\n";
    for (size_t i = 0; i < cluster.size(); ++i) {
      const uint64_t id = cluster[i];
      std::cout << "  " << docs[id].path;
      if (i > 0) std::cout << "  J=" << std::fixed << std::setprecision(3)
                           << sketches[cluster[0]].jaccard(sketches[id]);
      std::cout << "\n";
    }
    const std::string& body = docs[cluster[0]].body;
    std::string snippet = body.substr(0, std::min(body.size(), size_t(100)));
    for (size_t si = 0; si < snippet.size(); ++si)
      if (snippet[si] == '\n') snippet[si] = ' ';
    std::cout << "  \"" << snippet << "...\"\n\n";
    ++shown;
  }

  std::cout << "--- Example same-group duplicates ---\n\n";
  shown = 0;
  for (size_t ci = 0; ci < clusters.size() && shown < 5; ++ci) {
    const auto& cluster = clusters[ci];
    std::string first_group;
    {
      size_t slash = docs[cluster[0]].path.find('/');
      first_group = (slash != std::string::npos) ? docs[cluster[0]].path.substr(0, slash) : "";
    }
    bool all_same = true;
    for (size_t i = 1; i < cluster.size(); ++i) {
      size_t slash = docs[cluster[i]].path.find('/');
      std::string g = (slash != std::string::npos) ? docs[cluster[i]].path.substr(0, slash) : "";
      if (g != first_group) { all_same = false; break; }
    }
    if (!all_same) continue;

    std::cout << "Cluster " << ci + 1 << " (" << cluster.size() << " docs):\n";
    for (size_t i = 0; i < cluster.size(); ++i) {
      const uint64_t id = cluster[i];
      std::cout << "  " << docs[id].path;
      if (i > 0) std::cout << "  J=" << std::fixed << std::setprecision(3)
                           << sketches[cluster[0]].jaccard(sketches[id]);
      std::cout << "\n";
    }
    const std::string& body = docs[cluster[0]].body;
    std::string snippet = body.substr(0, std::min(body.size(), size_t(100)));
    for (size_t si = 0; si < snippet.size(); ++si)
      if (snippet[si] == '\n') snippet[si] = ' ';
    std::cout << "  \"" << snippet << "...\"\n\n";
    ++shown;
  }

  // =====================================================================
  // USE CASE 1: All-pairs — brute force vs LSH timing
  // =====================================================================
  const size_t n = docs.size();
  const size_t total_pairs = n * (n - 1) / 2;

  std::cout << "========================================\n";
  std::cout << "USE CASE 1: All-pairs deduplication\n";
  std::cout << "========================================\n\n";

  // Brute force: count pairs above threshold.
  std::cout << "Brute force: checking all " << total_pairs << " pairs ...\n";
  double t_brute_start = now_ms();
  size_t brute_matches = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (sketches[i].jaccard(sketches[j]) >= threshold)
        ++brute_matches;
    }
  }
  double brute_ms = now_ms() - t_brute_start;
  std::cout << "  " << brute_matches << " pairs above threshold\n";
  std::cout << "  Time: " << std::fixed << std::setprecision(0) << brute_ms << " ms\n\n";

  // LSH: already ran above.
  std::cout << "LSH: index + cluster (already computed above)\n";
  std::cout << "  " << clusters.size() << " clusters (" << total_in_clusters << " docs)\n";
  std::cout << "  Time: " << std::fixed << std::setprecision(0)
            << (index_ms + cluster_ms) << " ms\n\n";

  std::cout << "All-pairs comparison:\n";
  std::cout << "  Brute force: " << total_pairs << " Jaccard calls in "
            << std::fixed << std::setprecision(0) << brute_ms << " ms\n";
  std::cout << "  LSH:         index + cluster in "
            << std::fixed << std::setprecision(0) << (index_ms + cluster_ms) << " ms\n";
  double speedup = brute_ms / (index_ms + cluster_ms);
  std::cout << "  Speedup:     " << std::fixed << std::setprecision(1) << speedup << "x\n\n";

  // =====================================================================
  // USE CASE 2: Single-document query — brute force vs LSH
  // =====================================================================
  std::cout << "========================================\n";
  std::cout << "USE CASE 2: Single-document query\n";
  std::cout << "========================================\n\n";

  // Pick a document that has known duplicates (first cluster's first doc).
  const uint64_t query_id = clusters.empty() ? 0 : clusters[0][0];
  std::cout << "Query: [" << query_id << "] " << docs[query_id].path << "\n\n";

  // Brute force: scan all docs.
  std::cout << "Brute force: compare against all " << n << " docs ...\n";
  double t_query_brute = now_ms();
  size_t brute_query_matches = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i == query_id) continue;
    if (sketches[query_id].jaccard(sketches[i]) >= threshold)
      ++brute_query_matches;
  }
  double query_brute_ms = now_ms() - t_query_brute;
  std::cout << "  " << brute_query_matches << " matches found\n";
  std::cout << "  Time: " << std::fixed << std::setprecision(2) << query_brute_ms << " ms"
            << " (" << n << " Jaccard calls)\n\n";

  // LSH: query the index.
  std::cout << "LSH: " << num_bands << " hash lookups + verify candidates ...\n";
  double t_query_lsh = now_ms();
  const auto qcandidates = index.query_candidates(sketches[query_id]);
  // Verify candidates.
  size_t lsh_query_matches = 0;
  for (size_t ci = 0; ci < qcandidates.size(); ++ci) {
    if (qcandidates[ci] == query_id) continue;
    if (sketches[query_id].jaccard(sketches[qcandidates[ci]]) >= threshold)
      ++lsh_query_matches;
  }
  double query_lsh_ms = now_ms() - t_query_lsh;
  std::cout << "  " << lsh_query_matches << " matches found (" << qcandidates.size() << " candidates checked)\n";
  std::cout << "  Time: " << std::fixed << std::setprecision(4) << query_lsh_ms << " ms"
            << " (" << num_bands << " lookups + " << qcandidates.size() << " Jaccard calls)\n\n";

  std::cout << "Query comparison:\n";
  std::cout << "  Brute force: " << n << " comparisons in "
            << std::fixed << std::setprecision(2) << query_brute_ms << " ms\n";
  std::cout << "  LSH:         " << qcandidates.size() << " comparisons in "
            << std::fixed << std::setprecision(4) << query_lsh_ms << " ms\n";
  if (query_lsh_ms > 0.0001) {
    std::cout << "  Speedup:     " << std::fixed << std::setprecision(0)
              << (query_brute_ms / query_lsh_ms) << "x\n";
  }
  std::cout << "  Same result: " << (brute_query_matches == lsh_query_matches ? "yes" : "NO") << "\n\n";

  // -----------------------------------------------------------------------
  // Final summary.
  // -----------------------------------------------------------------------
  std::cout << "--- Summary ---\n";
  std::cout << "  Documents:         " << n << "\n";
  std::cout << "  LSH clusters:      " << clusters.size() << " (" << total_in_clusters << " docs)\n";
  std::cout << "  Sketch time:       " << std::fixed << std::setprecision(0) << sketch_ms << " ms\n";
  std::cout << "  LSH (index+cluster): " << std::fixed << std::setprecision(0)
            << (index_ms + cluster_ms) << " ms\n";
  std::cout << "  Brute force:       " << std::fixed << std::setprecision(0) << brute_ms << " ms\n";

  return 0;
}

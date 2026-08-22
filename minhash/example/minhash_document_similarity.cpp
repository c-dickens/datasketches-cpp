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
 * Near-duplicate document detection using MinHash sketches and LSH banding.
 *
 * Pipeline:
 *   text -> character 5-grams (shingles) -> MinHash sketch -> LSH index -> clusters
 *
 * LSH configuration: k=120 slots, 24 bands x 5 rows.
 * S-curve midpoint (P=0.5): J = (1/24)^(1/5) ~= 0.55.
 */

#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "minhash_lsh.hpp"

using namespace datasketches;

// ANSI colour helpers
static const char* col_reset  = "\033[0m";
static const char* col_green  = "\033[32m";
static const char* col_yellow = "\033[33m";
static const char* col_red    = "\033[31m";
static const char* col_dim    = "\033[2m";
static const char* col_bold   = "\033[1m";
static const char* col_cyan   = "\033[36m";

static const char* jaccard_colour(double j, double threshold) {
  if (j >= threshold)       return col_green;
  if (j >= threshold * 0.5) return col_yellow;
  return col_red;
}

// Build a MinHash sketch from a document string by feeding character 5-grams.
static minhash_sketch sketch_document(const std::string& text, uint16_t k) {
  auto sketch = minhash_sketch::builder().set_k(k).build();
  if (text.size() >= 5) {
    for (size_t i = 0; i + 5 <= text.size(); ++i)
      sketch.update(text.substr(i, 5));
  }
  return sketch;
}

// Print colour-coded similarity matrix.
static void print_similarity_matrix(
    const std::vector<minhash_sketch>& sketches,
    const std::vector<std::string>& labels,
    double threshold)
{
  const int n = static_cast<int>(sketches.size());
  const int label_w = 14;

  std::cout << col_bold << "--- Similarity matrix ---\n" << col_reset;

  std::cout << std::string(label_w, ' ');
  for (int j = 0; j < n; ++j)
    std::cout << col_dim << std::setw(7) << labels[j].substr(0, 6) << col_reset;
  std::cout << "\n";

  for (int i = 0; i < n; ++i) {
    std::cout << col_dim << std::setw(label_w) << std::left << labels[i].substr(0, label_w - 1) << col_reset;
    std::cout << std::right;
    for (int j = 0; j < n; ++j) {
      double jval = (i == j) ? 1.0 : sketches[i].jaccard(sketches[j]);
      const char* col = (i == j) ? col_dim : jaccard_colour(jval, threshold);
      std::cout << col << std::fixed << std::setprecision(2) << std::setw(7) << jval << col_reset;
    }
    std::cout << "\n";
  }
  std::cout << "\n";
}

// Print the LSH S-curve with pair annotations.
static void print_scurve(
    uint16_t num_bands, uint16_t rows_per_band, double threshold,
    const std::vector<minhash_sketch>& sketches,
    const std::vector<std::string>& labels)
{
  const int bar_w = 40;
  const int n = static_cast<int>(sketches.size());

  struct Pair { int i, j; double jval; };
  std::vector<Pair> pairs;
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      pairs.push_back({i, j, sketches[i].jaccard(sketches[j])});

  std::cout << col_bold << "--- S-curve: P(at least one band collision | J) ---\n" << col_reset;
  std::cout << col_dim << "    " << num_bands << " bands x " << rows_per_band
            << " rows  |  threshold=" << std::fixed << std::setprecision(2) << threshold << "\n" << col_reset;
  std::cout << "\n";

  const double step = 0.05;
  for (double jval = 0.0; jval <= 1.001; jval += step) {
    double p = 1.0 - std::pow(1.0 - std::pow(jval, rows_per_band), num_bands);
    int filled = static_cast<int>(std::round(p * bar_w));

    std::string annotation;
    for (const auto& pr : pairs) {
      if (std::abs(pr.jval - jval) < step * 0.5) {
        annotation += " <- ";
        annotation += labels[pr.i].substr(0, 5);
        annotation += " v ";
        annotation += labels[pr.j].substr(0, 5);
      }
    }

    bool above = (jval >= threshold - step * 0.5);
    std::cout << "  J=" << std::fixed << std::setprecision(2) << jval << "  |";
    std::cout << (above ? col_green : "");
    std::cout << std::string(filled, '#');
    std::cout << (above ? col_reset : "");
    std::cout << std::string(bar_w - filled, ' ');
    std::cout << "| " << std::setprecision(3) << std::setw(5) << p;
    if (std::abs(jval - threshold) < step * 0.5)
      std::cout << col_yellow << "  <- threshold" << col_reset;
    if (!annotation.empty())
      std::cout << col_dim << annotation << col_reset;
    std::cout << "\n";
  }
  std::cout << "\n";
}

int main() {
  struct Doc { uint64_t id; std::string title; std::string text; };
  const std::vector<Doc> corpus = {
    {0, "Fox-A",
     "The quick brown fox jumps over the lazy dog near the riverbank every morning at dawn."},
    {1, "Fox-B",
     "The quick brown fox jumps over the lazy dog near the riverbank every morning at dusk."},
    {2, "Climate-A",
     "Global average temperatures have risen by approximately 1.1 degrees Celsius since the "
     "pre-industrial era. Sea levels are rising at an accelerating rate. Extreme weather events "
     "are becoming more frequent and severe across the globe."},
    {3, "Climate-B",
     "Global average temperatures have risen by approximately 1.1 degrees Celsius since the "
     "pre-industrial era. Sea levels are rising at an accelerating rate. Scientists warn that "
     "immediate action is required to prevent irreversible damage."},
    {4, "Finance",
     "Central banks around the world raised interest rates aggressively throughout the year "
     "in an effort to bring inflation back to target levels after a prolonged period of "
     "accommodative monetary policy following the pandemic."},
    {5, "Fox-C",
     "A fast brown fox leaps across a sleepy dog by the river each morning before sunrise."},
  };

  const uint16_t k = 120;
  const uint16_t num_bands = 24;
  const uint16_t rows = k / num_bands;
  const double threshold = 0.7;

  std::vector<minhash_sketch> sketches;
  std::vector<std::string> labels;
  sketches.reserve(corpus.size());
  labels.reserve(corpus.size());

  std::cout << col_bold << "=== MinHash Document Similarity Demo ===\n\n" << col_reset;
  std::cout << "Configuration: k=" << k << ", " << num_bands << " bands x " << rows << " rows\n";
  std::cout << "S-curve midpoint (P=0.5): J ~= " << std::fixed << std::setprecision(2)
            << std::pow(1.0 / num_bands, 1.0 / rows) << "\n\n";

  std::cout << "--- Documents ---\n";
  for (const auto& doc : corpus) {
    auto sk = sketch_document(doc.text, k);
    std::cout << "  [" << doc.id << "] " << doc.title
              << "  (" << doc.text.size() << " chars, "
              << (doc.text.size() >= 5 ? doc.text.size() - 4 : 0) << " shingles)\n";
    sketches.push_back(std::move(sk));
    labels.push_back(doc.title);
  }
  std::cout << "\n";

  const int n = static_cast<int>(corpus.size());

  // -----------------------------------------------------------------------
  // Band key grid: bands (rows) x documents (columns).
  // Matching values in a row = those docs share a bucket in that band.
  // -----------------------------------------------------------------------
  std::vector<std::vector<uint64_t>> all_keys(n);
  for (int i = 0; i < n; ++i)
    all_keys[i] = minhash_lsh_index::band_keys(sketches[i], num_bands);

  // Track which (band, key) pairs appear more than once = collisions.
  std::map<std::pair<int,uint64_t>, int> key_counts;
  for (int i = 0; i < n; ++i)
    for (int b = 0; b < num_bands; ++b)
      ++key_counts[{b, all_keys[i][b]}];

  std::cout << col_bold << "--- Band keys (bands x documents) ---\n" << col_reset;
  std::cout << col_dim << "  Each cell is hash(" << rows << " signature slots) mod 1000.\n"
            << "  Same number in a row = those docs share a bucket in that band.\n" << col_reset;

  // Header: document names.
  std::cout << "        ";
  for (int i = 0; i < n; ++i)
    std::cout << std::setw(8) << labels[i].substr(0, 7);
  std::cout << "\n";

  // One row per band.
  for (int b = 0; b < num_bands; ++b) {
    std::cout << "  B" << std::setw(2) << std::left << b << std::right << "   ";
    for (int i = 0; i < n; ++i) {
      int short_key = static_cast<int>(all_keys[i][b] % 1000);
      bool collision = key_counts[{b, all_keys[i][b]}] > 1;
      if (collision)
        std::cout << col_green << std::setw(8) << short_key << col_reset;
      else
        std::cout << col_dim << std::setw(8) << short_key << col_reset;
    }
    std::cout << "\n";
  }
  std::cout << col_dim << "  (green = collision: those docs share a bucket in that band)\n" << col_reset;
  std::cout << "\n";

  print_similarity_matrix(sketches, labels, threshold);
  print_scurve(num_bands, rows, threshold, sketches, labels);

  // =====================================================================
  // USE CASE 1: All-pairs deduplication (batch)
  //
  // Question: "Find every near-duplicate pair in a corpus."
  // Brute force: compare every pair — O(n^2) Jaccard calls.
  // LSH: build band index, only verify pairs that share a bucket — O(n*b)
  //       bucket walk + far fewer Jaccard calls.
  // =====================================================================
  std::cout << col_bold << "========================================\n";
  std::cout << "USE CASE 1: All-pairs deduplication\n";
  std::cout << "========================================\n\n" << col_reset;

  // --- Step 1a: Brute force approach ---
  const int total_pairs = n * (n - 1) / 2;
  std::cout << col_bold << "Brute force: check all " << total_pairs << " pairs ---\n" << col_reset;
  int brute_matches = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const double jval = sketches[i].jaccard(sketches[j]);
      const bool above = (jval >= threshold);
      if (above) ++brute_matches;
      std::cout << "  (" << i << "," << j << ") "
                << std::setw(11) << std::left << labels[i].substr(0, 10) << " vs "
                << std::setw(11) << std::left << labels[j].substr(0, 10) << std::right
                << "  J=" << std::fixed << std::setprecision(3) << jval;
      if (above)
        std::cout << col_green << "  MATCH" << col_reset;
      else
        std::cout << col_dim << "  skip" << col_reset;
      std::cout << "\n";
    }
  }
  std::cout << col_dim << "  " << total_pairs << " Jaccard comparisons\n" << col_reset << "\n";

  // --- Step 1b: LSH approach — build the band index ---
  minhash_lsh_index index(num_bands, threshold);
  for (int i = 0; i < n; ++i)
    index.add(corpus[i].id, sketches[i]);

  // Show the band index ("mini database"): compute band keys for each doc
  // and show which docs land in the same bucket.
  std::cout << col_bold << "LSH band index (the \"mini database\") ---\n" << col_reset;
  std::cout << col_dim << "  Each band hashes " << rows << " contiguous signature slots into one key.\n"
            << "  Docs with the same key in any band become candidates.\n" << col_reset << "\n";

  // Build a local view of the band buckets for display.
  // band -> key -> list of doc ids
  using bucket_contents = std::map<uint64_t, std::vector<int>>;
  std::vector<bucket_contents> band_view(num_bands);
  for (int i = 0; i < n; ++i) {
    const auto keys = minhash_lsh_index::band_keys(sketches[i], num_bands);
    for (uint16_t b = 0; b < num_bands; ++b)
      band_view[b][keys[b]].push_back(i);
  }

  // Show bands that have collisions, summarise the rest.
  int bands_with_collisions = 0;
  int total_colliding_buckets = 0;
  // Collect candidate pairs from collisions for later.
  std::set<std::pair<int,int>> lsh_candidate_set;

  for (uint16_t b = 0; b < num_bands; ++b) {
    bool has_collision = false;
    for (const auto& entry : band_view[b]) {
      if (entry.second.size() >= 2) {
        has_collision = true;
        ++total_colliding_buckets;
        // Record candidate pairs.
        for (size_t x = 0; x < entry.second.size(); ++x)
          for (size_t y = x + 1; y < entry.second.size(); ++y)
            lsh_candidate_set.insert({entry.second[x], entry.second[y]});
      }
    }
    if (has_collision) ++bands_with_collisions;
  }

  // Show a few representative bands.
  int bands_shown = 0;
  for (uint16_t b = 0; b < num_bands && bands_shown < 5; ++b) {
    bool has_collision = false;
    for (const auto& entry : band_view[b])
      if (entry.second.size() >= 2) { has_collision = true; break; }
    if (!has_collision && bands_shown >= 2) continue; // skip boring bands after showing a couple

    std::cout << "  Band " << std::setw(2) << b << ": ";
    bool first_bucket = true;
    for (const auto& entry : band_view[b]) {
      if (!first_bucket) std::cout << "          ";
      first_bucket = false;
      std::cout << "key " << std::hex << std::setw(6) << (entry.first & 0xFFFFFF)
                << std::dec << " -> [";
      for (size_t x = 0; x < entry.second.size(); ++x) {
        if (x) std::cout << ", ";
        std::cout << entry.second[x] << " " << labels[entry.second[x]].substr(0, 5);
      }
      std::cout << "]";
      if (entry.second.size() >= 2)
        std::cout << col_green << "  <- COLLISION" << col_reset;
      std::cout << "\n";
    }
    ++bands_shown;
  }
  std::cout << col_dim << "  ... (" << bands_with_collisions << " of " << num_bands
            << " bands have collisions, " << total_colliding_buckets << " colliding buckets total)\n"
            << col_reset << "\n";

  // --- Step 1c: LSH verification — only check candidate pairs ---
  std::cout << col_bold << "LSH verification: check only " << lsh_candidate_set.size()
            << " candidate pair(s) ---\n" << col_reset;
  int lsh_matches = 0;
  for (const auto& p : lsh_candidate_set) {
    const double jval = sketches[p.first].jaccard(sketches[p.second]);
    const bool above = (jval >= threshold);
    if (above) ++lsh_matches;
    std::cout << "  (" << p.first << "," << p.second << ") "
              << std::setw(11) << std::left << labels[p.first].substr(0, 10) << " vs "
              << std::setw(11) << std::left << labels[p.second].substr(0, 10) << std::right
              << "  J=" << std::fixed << std::setprecision(3) << jval;
    if (above)
      std::cout << col_green << "  MATCH" << col_reset;
    else
      std::cout << col_yellow << "  filtered (below threshold)" << col_reset;
    std::cout << "\n";
  }
  std::cout << "\n";

  // --- All-pairs summary ---
  const int skipped = total_pairs - static_cast<int>(lsh_candidate_set.size());
  std::cout << col_bold << "All-pairs summary:\n" << col_reset;
  std::cout << "  Brute force: " << total_pairs << " Jaccard comparisons -> " << brute_matches << " match(es)\n";
  std::cout << "  LSH:         " << lsh_candidate_set.size() << " Jaccard comparisons -> " << lsh_matches << " match(es)"
            << col_dim << "  (skipped " << skipped << " pairs that never shared a bucket)" << col_reset << "\n";
  std::cout << "  Same answer: " << (brute_matches == lsh_matches ? "yes" : "NO") << "\n\n";

  // Clusters.
  std::cout << col_bold << "Duplicate clusters:\n" << col_reset;
  const auto clusters = index.get_duplicate_clusters();
  if (clusters.empty()) {
    std::cout << "  (none)\n";
  } else {
    for (const auto& cluster : clusters) {
      std::cout << col_green << "  {" << col_reset;
      for (size_t ci = 0; ci < cluster.size(); ++ci) {
        if (ci) std::cout << ", ";
        std::cout << col_green << cluster[ci] << " (" << corpus[cluster[ci]].title << ")" << col_reset;
      }
      std::cout << "}\n";
    }
  }
  std::cout << "\n";

  // =====================================================================
  // USE CASE 2: Single-document query
  //
  // Question: "Given a new document, find its near-duplicates in the index."
  // Brute force: compare against every indexed document — O(n) Jaccard calls.
  // LSH: compute b band keys, do b hash lookups — O(b), independent of n.
  // =====================================================================
  std::cout << col_bold << "========================================\n";
  std::cout << "USE CASE 2: Single-document query\n";
  std::cout << "========================================\n\n" << col_reset;

  const std::string query_text =
      "The quick brown fox jumps over the lazy dog near the riverbank every morning at dawn.";
  std::cout << "Query: \"" << query_text.substr(0, 60) << "...\"\n\n";
  auto query_sketch = sketch_document(query_text, k);

  // --- Step 2a: Brute force ---
  std::cout << col_bold << "Brute force: compare query against all " << n << " indexed docs ---\n" << col_reset;
  for (int i = 0; i < n; ++i) {
    const double jval = query_sketch.jaccard(sketches[i]);
    const bool above = (jval >= threshold);
    std::cout << "  vs [" << i << "] " << std::setw(11) << std::left << labels[i].substr(0, 10) << std::right
              << "  J=" << std::fixed << std::setprecision(3) << jval;
    if (above) std::cout << col_green << "  MATCH" << col_reset;
    else       std::cout << col_dim   << "  skip"  << col_reset;
    std::cout << "\n";
  }
  std::cout << col_dim << "  " << n << " Jaccard comparisons\n" << col_reset << "\n";

  // --- Step 2b: LSH query ---
  const auto query_keys = minhash_lsh_index::band_keys(query_sketch, num_bands);
  const auto query_candidates = index.query_candidates(query_sketch);

  std::cout << col_bold << "LSH query: " << num_bands << " hash lookups (one per band) ---\n" << col_reset;
  std::cout << col_dim << "  Compute " << num_bands << " band keys for the query document.\n"
            << "  Look up each key in the corresponding band's hash table.\n"
            << "  Collect doc ids from buckets that match.\n" << col_reset << "\n";

  // Show which bands produced hits.
  int bands_with_hits = 0;
  for (uint16_t b = 0; b < num_bands; ++b) {
    auto it = band_view[b].find(query_keys[b]);
    if (it != band_view[b].end() && it->second.size() >= 2) ++bands_with_hits;
  }

  // Show a few band lookups.
  int lookups_shown = 0;
  for (uint16_t b = 0; b < num_bands && lookups_shown < 6; ++b) {
    auto it = band_view[b].find(query_keys[b]);
    bool hit = (it != band_view[b].end() && it->second.size() >= 2);
    if (!hit && lookups_shown >= 3) continue;

    std::cout << "  Band " << std::setw(2) << b << ": lookup key "
              << std::hex << std::setw(6) << (query_keys[b] & 0xFFFFFF) << std::dec << " -> ";
    if (it != band_view[b].end()) {
      std::cout << "[";
      for (size_t x = 0; x < it->second.size(); ++x) {
        if (x) std::cout << ", ";
        std::cout << it->second[x] << " " << labels[it->second[x]].substr(0, 5);
      }
      std::cout << "]";
      if (hit) std::cout << col_cyan << "  <- found candidates" << col_reset;
    } else {
      std::cout << col_dim << "(empty)" << col_reset;
    }
    std::cout << "\n";
    ++lookups_shown;
  }
  std::cout << col_dim << "  ... (" << bands_with_hits << " of " << num_bands
            << " bands returned candidates)\n" << col_reset << "\n";

  // Verify only the candidates.
  // Remove self from candidates (query is doc 0 itself in this case).
  std::cout << col_bold << "LSH verify: check " << query_candidates.size() << " candidate(s) ---\n" << col_reset;
  for (const uint64_t cid : query_candidates) {
    const double jval = query_sketch.jaccard(sketches[static_cast<int>(cid)]);
    const bool above = (jval >= threshold);
    std::cout << "  vs [" << cid << "] " << std::setw(11) << std::left << labels[cid].substr(0, 10) << std::right
              << "  J=" << std::fixed << std::setprecision(3) << jval;
    if (above) std::cout << col_green << "  MATCH" << col_reset;
    else       std::cout << col_yellow << "  filtered" << col_reset;
    std::cout << "\n";
  }
  std::cout << "\n";

  std::cout << col_bold << "Query summary:\n" << col_reset;
  std::cout << "  Brute force: " << n << " Jaccard comparisons (one per indexed doc)\n";
  std::cout << "  LSH:         " << num_bands << " hash lookups + "
            << query_candidates.size() << " Jaccard verifications\n";
  std::cout << col_dim << "  At n=1M docs, brute force = 1M comparisons. LSH still = "
            << num_bands << " lookups + a few verifications.\n" << col_reset;
  std::cout << "\n";

  return 0;
}

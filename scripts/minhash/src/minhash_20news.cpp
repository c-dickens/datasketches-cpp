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

// Checks the MinHash sketch on 20 Newsgroups: its Jaccard estimates against the
// exact values, per J bin and per k, and that merging chunk sketches is exact.
// Usage: minhash_20news <20_newsgroups directory> <output .md file>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "news20/loader.hpp"

namespace {

// The results file is committed, so it starts with the ASF licence header, as tools/rat-check.sh requires.
const char* const LICENCE_HEADER =
    "<!--\n"
    "Licensed to the Apache Software Foundation (ASF) under one\n"
    "or more contributor license agreements.  See the NOTICE file\n"
    "distributed with this work for additional information\n"
    "regarding copyright ownership.  The ASF licenses this file\n"
    "to you under the Apache License, Version 2.0 (the\n"
    "\"License\"); you may not use this file except in compliance\n"
    "with the License.  You may obtain a copy of the License at\n"
    "\n"
    "  http://www.apache.org/licenses/LICENSE-2.0\n"
    "\n"
    "Unless required by applicable law or agreed to in writing,\n"
    "software distributed under the License is distributed on an\n"
    "\"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY\n"
    "KIND, either express or implied.  See the License for the\n"
    "specific language governing permissions and limitations\n"
    "under the License.\n"
    "-->\n\n";

using sketch = datasketches::minhash_sketch;
using shingle_set = std::vector<std::string>;

// The sketch of the 4 consecutive pieces of a sorted set, merged in order into a new empty sketch.
sketch merged_pieces(const shingle_set& set, uint16_t k) {
  auto merged = datasketches::minhash_sketch::builder().set_k(k).build();
  const std::size_t n = set.size();
  for (std::size_t c = 0; c < demo::MERGE_PIECES; ++c) {
    const std::size_t begin = c * n / demo::MERGE_PIECES;
    const std::size_t end = (c + 1) * n / demo::MERGE_PIECES;
    const shingle_set piece(set.begin() + static_cast<std::ptrdiff_t>(begin),
                            set.begin() + static_cast<std::ptrdiff_t>(end));
    merged.merge(demo::sketch_of(piece, k));
  }
  return merged;
}

int run(const std::string& data_dir, const std::string& out_path) {
  const auto posts = news20::load_posts(data_dir);
  std::vector<shingle_set> sets;
  std::vector<std::string> ids;
  sets.reserve(posts.size());
  ids.reserve(posts.size());
  for (const auto& p : posts) {
    sets.push_back(news20::shingle_text(p.body));
    ids.push_back(p.message_id);
  }

  const auto pairs = demo::check_pairs(ids, demo::SAMPLE_SIZE, demo::SAMPLE_SEED);
  const auto message_id_pairs = demo::check_pairs(ids, 0, 0).size();  // the Message-ID pairs alone
  const std::size_t sampled = demo::SAMPLE_SIZE * (demo::SAMPLE_SIZE - 1) / 2;

  std::vector<double> exact;
  exact.reserve(pairs.size());
  std::size_t exact_zero = 0;
  std::size_t exact_one = 0;
  for (const auto& pr : pairs) {
    const double j = news20::jaccard(sets[pr.first], sets[pr.second]);
    exact.push_back(j);
    if (j <= 0.0) {
      ++exact_zero;
    } else if (j >= 1.0) {
      ++exact_one;
    }
  }

  std::ostringstream md;
  md << LICENCE_HEADER;
  md << "# MinHash correctness on 20 Newsgroups\n\n";
  md << "Written by `build/minhash_20news` (`./run_demos.sh`). The ticket is MH-8.\n\n";
  md << "## Pairs\n\n";
  md << "- posts: " << posts.size() << "\n";
  md << "- sampled pairs (all pairs of " << demo::SAMPLE_SIZE << " posts, seed " << demo::SAMPLE_SEED
     << "): " << sampled << "\n";
  md << "- `Message-ID` pairs (posts that share a `Message-ID` header): " << message_id_pairs << "\n";
  md << "- total pairs (the union, without duplicates): " << pairs.size() << "\n";
  md << "- pairs with exact J = 0, left out of the bins: " << exact_zero << "\n";
  md << "- pairs with exact J = 1, left out of the bins: " << exact_one << "\n\n";
  md << "## Criteria\n\n";
  md << "Each J bin [0.1 b, 0.1 (b + 1)) with at least " << demo::MIN_BIN_PAIRS
     << " pairs is checked. A bin with fewer pairs is reported as \"not checked\".\n\n";
  md << "- bias: |mean(estimate - exact)| <= " << demo::MAX_MEAN_BIAS << "\n";
  md << "- spread: |observed SD / expected SD - 1| <= " << demo::SPREAD_TOLERANCE
     << ", with expected SD = sqrt(mean(J (1 - J)) / k)\n";
  md << "- coverage 2 SD: `jaccard_bounds(b, 2)` covers the exact J for at least "
     << demo::NOMINAL_COVERAGE_2SD - demo::COVERAGE_SLACK << " of the pairs\n";
  md << "- coverage 3 SD: `jaccard_bounds(b, 3)` covers the exact J for at least "
     << demo::NOMINAL_COVERAGE_3SD - demo::COVERAGE_SLACK << " of the pairs\n\n";

  bool overall = true;
  std::size_t merge_mismatches = 0;
  for (const uint16_t k : demo::K_VALUES) {
    std::vector<sketch> sketches;
    sketches.reserve(sets.size());
    for (const auto& set : sets) {
      sketches.push_back(demo::sketch_of(set, k));
    }

    std::vector<demo::pair_estimate> estimates;
    estimates.reserve(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
      const auto& a = sketches[pairs[i].first];
      const auto& b = sketches[pairs[i].second];
      estimates.push_back(demo::pair_estimate{exact[i], a.jaccard(b), a.jaccard_bounds(b, 2), a.jaccard_bounds(b, 3)});
    }
    const auto bins = demo::summarise_bins(estimates, k);
    bool k_pass = true;
    for (const auto& bin : bins) {
      k_pass = k_pass && demo::bin_passes(bin);
    }
    overall = overall && k_pass;
    md << "## k = " << k << ": " << (k_pass ? "PASS" : "FAIL") << "\n\n" << demo::bins_table(bins) << "\n";
    std::cout << "k = " << k << ": " << (k_pass ? "PASS" : "FAIL") << "\n";

    for (std::size_t p = 0; p < sets.size(); ++p) {
      const auto merged = merged_pieces(sets[p], k);
      if (merged.is_empty() != sketches[p].is_empty() || merged.get_signature() != sketches[p].get_signature()) {
        ++merge_mismatches;
      }
    }
  }

  const bool merge_pass = merge_mismatches == 0;
  overall = overall && merge_pass;
  md << "## Merge\n\n";
  md << "Each post's sorted shingle set is split into " << demo::MERGE_PIECES
     << " consecutive pieces. The merge of the piece sketches must equal the sketch of the whole set, at every k.\n\n";
  md << "- posts checked: " << sets.size() << " at each of " << demo::K_VALUES.size() << " values of k\n";
  md << "- mismatches: " << merge_mismatches << "\n";
  md << "- result: " << (merge_pass ? "PASS" : "FAIL") << "\n\n";
  md << "Overall: " << (overall ? "PASS" : "FAIL") << "\n";
  std::cout << "merge: " << (merge_pass ? "PASS" : "FAIL") << "\n";
  std::cout << "Overall: " << (overall ? "PASS" : "FAIL") << "\n";

  std::ofstream out(out_path, std::ios::binary);
  out << md.str();
  if (!out) {
    std::cerr << "error: cannot write " << out_path << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <20_newsgroups directory> <output .md file>\n";
    return 2;
  }
  try {
    return run(argv[1], argv[2]);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}

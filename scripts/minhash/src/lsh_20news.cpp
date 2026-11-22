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

// Checks the LSH index: the candidate rate of synthetic integer sets against the S-curve
// 1 - (1 - J^r)^b, and, on 20 Newsgroups, that cross-posts land in one cluster. It also
// reports recall, precision and false collisions on the pairs of MH-8, and the largest buckets.
// Usage: lsh_20news <20_newsgroups directory> <output .md file>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "minhash_lsh.hpp"
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

using datasketches::minhash_lsh_index;
using shingle_set = std::vector<std::string>;

std::string fixed(double value, int digits) {
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(digits);
  os << value;
  return os.str();
}

std::string ratio(std::size_t num, std::size_t den) {
  return den == 0 ? std::string("-") : fixed(static_cast<double>(num) / static_cast<double>(den), 4);
}

const char* verdict(bool pass) {
  return pass ? "PASS" : "FAIL";
}

int run(const std::string& data_dir, const std::string& out_path) {
  const auto scurve = demo::scurve_check(
      [](const datasketches::minhash_sketch& s) { return minhash_lsh_index::band_keys(s, demo::LSH_BANDS); },
      demo::LSH_ROWS, demo::LSH_BANDS);
  const bool scurve_pass = demo::scurve_passes(scurve);

  const auto posts = news20::load_posts(data_dir);
  std::vector<shingle_set> sets;
  std::vector<std::string> ids;
  sets.reserve(posts.size());
  ids.reserve(posts.size());
  for (const auto& p : posts) {
    sets.push_back(news20::shingle_text(p.body));
    ids.push_back(p.message_id);
  }
  const std::size_t n_posts = posts.size();

  std::vector<datasketches::minhash_sketch> sketches;
  sketches.reserve(n_posts);
  minhash_lsh_index index(demo::LSH_BANDS, demo::LSH_THRESHOLD);
  for (std::size_t i = 0; i < n_posts; ++i) {
    sketches.push_back(demo::sketch_of(sets[i], demo::LSH_K));
    if (!sketches[i].is_empty()) {  // add throws for an empty sketch
      index.add(i, sketches[i]);
    }
  }
  const auto clusters = index.get_duplicate_clusters();
  const std::size_t indexed = index.get_num_documents();

  std::vector<std::vector<uint64_t>> keys(n_posts);
  for (std::size_t i = 0; i < n_posts; ++i) {
    if (!sketches[i].is_empty()) {
      keys[i] = minhash_lsh_index::band_keys(sketches[i], demo::LSH_BANDS);
    }
  }

  // The pairs of MH-8, without the pairs with a post that is not indexed.
  const auto pairs = demo::check_pairs(ids, demo::SAMPLE_SIZE, demo::SAMPLE_SEED);
  demo::pair_counts counts;
  std::size_t left_out = 0;
  std::size_t disjoint = 0;            // pairs with exact J = 0
  std::size_t disjoint_candidates = 0;  // of them, the candidates: false collisions of the band keys
  for (const auto& pr : pairs) {
    if (keys[pr.first].empty() || keys[pr.second].empty()) {
      ++left_out;
      continue;
    }
    const double exact = news20::jaccard(sets[pr.first], sets[pr.second]);
    const bool candidate = demo::is_candidate(keys[pr.first], keys[pr.second]);
    const bool verified = candidate && sketches[pr.first].jaccard(sketches[pr.second]) >= demo::LSH_THRESHOLD;
    if (exact == 0.0) {
      ++disjoint;
      disjoint_candidates += candidate ? 1 : 0;
    }
    counts.add(exact >= demo::LSH_THRESHOLD, verified);
  }

  // Cross-posts: each pair of indexed posts that share a Message-ID must be in one cluster.
  std::map<std::size_t, std::size_t> cluster_of;
  for (std::size_t c = 0; c < clusters.size(); ++c) {
    for (const uint64_t id : clusters[c]) {
      cluster_of[static_cast<std::size_t>(id)] = c;
    }
  }
  std::size_t cross_checked = 0;
  std::size_t cross_failed = 0;
  for (const auto& pr : demo::check_pairs(ids, 0, 0)) {  // the Message-ID pairs alone
    if (keys[pr.first].empty() || keys[pr.second].empty()) {
      continue;
    }
    ++cross_checked;
    const auto a = cluster_of.find(pr.first);
    const auto b = cluster_of.find(pr.second);
    if (a == cluster_of.end() || b == cluster_of.end() || a->second != b->second) {
      ++cross_failed;
    }
  }
  const bool cross_pass = cross_failed == 0;

  const auto buckets = demo::largest_buckets(keys, demo::LARGEST_BUCKETS);

  const bool overall = scurve_pass && cross_pass;

  std::ostringstream md;
  md << LICENCE_HEADER;
  md << "# LSH correctness on 20 Newsgroups\n\n";
  md << "Written by `build/lsh_20news` (`./run_demos.sh`). The ticket is MH-14.\n\n";
  md << "Index: k = " << demo::LSH_K << ", " << demo::LSH_BANDS << " bands of " << demo::LSH_ROWS
     << " slots, threshold " << fixed(demo::LSH_THRESHOLD, 1) << ".\n\n";
  md << "## S-curve (synthetic sets): " << verdict(scurve_pass) << "\n\n";
  md << "Each pair is 2 synthetic integer sets S1 = [o, o + a + c) and S2 = [o + a, o + 2a + c), with 2a + c = "
     << demo::SCURVE_UNION << ", so exact J = c / " << demo::SCURVE_UNION << ". Each bin has "
     << demo::SCURVE_PAIRS_PER_BIN << " pairs, with J spread across the bin, and no item is in 2 pairs. "
        "A pair is a candidate when its band keys are equal in at least one band. The candidate count of a bin "
        "is a sum of independent Bernoulli(p_i), with p_i = 1 − (1 − J_i^"
     << demo::LSH_ROWS << ")^" << demo::LSH_BANDS << ". A bin passes when its exact two-sided p-value is at least "
     << demo::SCURVE_ALPHA << " / " << demo::NUM_BINS
     << ", so a correct index fails the table by chance with a probability of at most " << demo::SCURVE_ALPHA
     << ".\n\n";
  md << demo::scurve_table(scurve) << "\n";

  md << "## Posts and pairs\n\n";
  md << "- posts: " << n_posts << "\n";
  md << "- indexed posts (posts with at least one shingle): " << indexed << "\n";
  md << "- posts not indexed (no shingles): " << n_posts - indexed << "\n";
  md << "- pairs (the pairs of MH-8: all pairs of " << demo::SAMPLE_SIZE << " sampled posts, seed "
     << demo::SAMPLE_SEED << ", plus the `Message-ID` pairs): " << pairs.size() << "\n";
  md << "- pairs left out (a post is not indexed): " << left_out << "\n";
  md << "- pairs checked: " << pairs.size() - left_out << "\n\n";

  md << "## Cross-posts: " << verdict(cross_pass) << "\n\n";
  md << "Each pair of indexed posts that share a `Message-ID` must be in one cluster of "
        "`get_duplicate_clusters()`.\n\n";
  md << "- pairs checked: " << cross_checked << "\n";
  md << "- pairs not in one cluster: " << cross_failed << "\n";
  md << "- clusters: " << clusters.size() << "\n\n";

  md << "## Recall and precision\n\n";
  md << "Similar: exact J ≥ " << fixed(demo::LSH_THRESHOLD, 1)
     << ". Verified: candidate and estimated J ≥ " << fixed(demo::LSH_THRESHOLD, 1) << ".\n\n";
  md << "- similar pairs: " << counts.similar << "\n";
  md << "- verified pairs: " << counts.verified << "\n";
  md << "- both: " << counts.both << "\n";
  md << "- recall (both / similar): " << ratio(counts.both, counts.similar) << "\n";
  md << "- precision (both / verified): " << ratio(counts.both, counts.verified) << "\n\n";

  md << "## False collisions\n\n";
  md << "Candidates among the pairs with exact J = 0. The band keys of such a pair are equal only when "
        "2 different 64-bit hashes collide.\n\n";
  md << "- pairs with exact J = 0: " << disjoint << "\n";
  md << "- candidates among them: " << disjoint_candidates << "\n\n";

  md << "## The " << demo::LARGEST_BUCKETS << " largest buckets\n\n";
  md << "| band | size | first posts |\n|---:|---:|---|\n";
  for (const auto& bk : buckets) {
    md << "| " << bk.band << " | " << bk.items.size() << " | ";
    for (std::size_t e = 0; e < bk.items.size() && e < demo::BUCKET_EXAMPLES; ++e) {
      const auto& p = posts[bk.items[e]];
      md << (e == 0 ? "" : ", ") << "`" << p.group << "/" << p.id << "`";
    }
    md << " |\n";
  }
  md << "\n";

  md << "Overall: " << verdict(overall) << "\n";

  std::cout << "S-curve: " << verdict(scurve_pass) << "\n";
  std::cout << "cross-posts: " << verdict(cross_pass) << "\n";
  std::cout << "recall " << ratio(counts.both, counts.similar) << ", precision "
            << ratio(counts.both, counts.verified) << "\n";
  std::cout << "false collisions " << disjoint_candidates << " of " << disjoint << "\n";
  std::cout << "Overall: " << verdict(overall) << "\n";

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

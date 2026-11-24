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

// MH-15b: the MH-14b check on agent trajectories (SWE-rebench OpenHands, the MH-15 sample of
// 1,000 task instances): the sketch and the LSH clusters against the exact Jaccard of every pair of
// indexed trajectories, at every (k, r, t) of demo::full_grid(). demo::full_main runs the check.
// Usage: lsh_trajectories_full [--smoke] <trajectories_action_text directory> <results directory>
// --smoke uses only the demo::SAMPLE_SIZE trajectories of demo::sample_indices().
//
// The item (MH-E decision 1, action_text_words_w1 of MH-E1): the words of the agent's own tool-call
// arguments, cut by demo::words, as a set. The directory holds action_text.tsv, which the data
// bridge analysis/trajectory_action_text.py writes: one line per trajectory with trajectory_id,
// instance_id, repo, resolved and the action text, separated by tabs.

#include <cstddef>
#include <filesystem>
#include <map>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "demo/words.hpp"
#include "news20/loader.hpp"

namespace {

namespace fs = std::filesystem;

// The SHA-256 of trajectories.parquet: PARQUET_SHA256 in data/get_trajectories.sh, which checks it.
const char* const PARQUET_SHA256 = "14048dd1fcd22ce094b6e85f8a38f223a9ef1327031aaaad052804870212efa1";
constexpr std::size_t FIELDS = 5;

struct trajectory {
  std::string id;        // trajectory_id
  std::string instance;  // instance_id
  std::string text;      // the action text
};

std::vector<trajectory> load_trajectories(const fs::path& dir) {
  const fs::path path = dir / "action_text.tsv";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string() + "; run analysis/trajectory_action_text.py first");
  }
  std::vector<trajectory> out;
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (std::size_t f = 0; f + 1 < FIELDS; ++f) {
      const std::size_t tab = line.find('\t', start);
      if (tab == std::string::npos) {
        throw std::runtime_error(path.string() + ": line " + std::to_string(out.size() + 1) + " has too few fields");
      }
      fields.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    out.push_back(trajectory{fields[0], fields[1], line.substr(start)});
  }
  return out;
}

demo::full_source load(const fs::path& data_dir, bool smoke) {
  const auto trajs = load_trajectories(data_dir);
  demo::full_source src;
  src.ticket = "MH-15b";
  src.stem = "lsh_trajectories_full";
  src.dataset = "agent trajectories";
  src.noun = "trajectory";
  src.nouns = "trajectories";
  src.sample_note = "the demo::sample_indices sample";
  src.archive_sha256 = PARQUET_SHA256;
  src.shingle_words = 1;
  src.shingle_source = "in the agent's own tool-call arguments (the text that `analysis/trajectory_action_text.py` writes; "
                       "tool replies are not used)";
  src.shingle_sets = "the word sets of the action text (`demo::words`, MH-E decision 1)";
  // CD-416 owner decisions of 24 Nov 2026.
  src.rows = {4, 8, 16};  // r = 2 is left out: at background J of about 0.27, almost every pair is a candidate
  src.thresholds = {0.4, 0.45, 0.5, 0.55, 0.6};
  src.ref_k = 128;  // the MH-E1 values: t = 0.4, k = 128, (b, r) = (32, 4)
  src.ref_rows = 4;
  src.ref_threshold = 0.4;
  src.ref_label = "MH-E1 setting";
  src.compare_k = 512;
  src.compare_rows = 4;
  src.floor = 0.35;
  src.pair_test_in_overall = false;
  src.pair_test_note =
      "The correlation is strong here, because all trajectories share a common vocabulary (the background J is "
      "about 0.27). A smoke-run diagnosis (2,000 trajectories, k = 128, 30 sketch seeds, a pair counted when "
      "Ĵ ≥ t) found that the mean counts over the seeds match the model, for example 1,954 against 1,951 verified "
      "true pairs at t = 0.4, but their spread is 5 to 400 times the independent-pair SD. So the p-values below say "
      "little, and `Overall` covers only the prefix assert, the identical-set check and the index-consistency "
      "assert. A better test is future work.\n\n";
  src.proxy_title = "Same-instance proxy";
  src.proxy_text =
      "The data has no similarity labels. The proxy says that two trajectories of the same task instance are "
      "related. It is not the truth of this report (the truth is exact J ≥ t), and different instances can still "
      "share a near-identical path.";
  const auto chosen = demo::full_chosen(trajs.size(), smoke);
  src.loaded = trajs.size();
  src.chosen = chosen.size();
  std::vector<std::string> keys;  // the word set as one string
  std::map<std::string, std::size_t> group_of;  // instance_id -> its first node
  for (const std::size_t d : chosen) {
    auto set = news20::shingles(demo::words(trajs[d].text), 1);
    if (!set.empty()) {
      std::string key;
      for (const auto& s : set) {
        key += s + "\n";
      }
      keys.push_back(std::move(key));
      src.item_of.push_back(d);
      src.sets.push_back(std::move(set));
      src.names.push_back(trajs[d].instance + "/" + trajs[d].id);
      src.proxy_group.push_back(group_of.emplace(trajs[d].instance, src.sets.size() - 1).first->second);
    }
  }
  src.external = demo::check_pairs(keys, 0, 0);  // the pairs of indexed trajectories with identical word sets
  src.external_title = "Identical word sets";
  src.external_text = "Each pair of indexed trajectories with identical word sets must be in one true cluster and in "
                      "one LSH cluster, at every grid point.";
  src.external_label = "pairs with identical word sets";
  src.external_column = "IdenticalSetsSplit";
  return src;
}

}  // namespace

int main(int argc, char** argv) {
  return demo::full_main(argc, argv, "trajectories_action_text", load);
}

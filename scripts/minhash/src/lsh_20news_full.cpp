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
// 20 Newsgroups posts, at every (k, r, t) of demo::full_grid(). demo::full_main runs the check.
// Usage: lsh_20news_full [--smoke] <20_newsgroups directory> <results directory>
// --smoke uses only the demo::SAMPLE_SIZE posts of demo::sample_indices().

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "news20/loader.hpp"

namespace {

// The SHA-256 of 20_newsgroups.tar.gz: UCI_TAR_SHA256 in data/get_20news.sh, which checks it.
const char* const ARCHIVE_SHA256 = "ab7bbf82b7831f7dbb1a09d9312f66fa78565c8de25526999b0d66f69d37e414";

demo::full_source load(const std::filesystem::path& data_dir, bool smoke) {
  const auto posts = news20::load_posts(data_dir);
  demo::full_source src;
  src.ticket = "MH-14b";
  src.stem = "lsh_20news_full";
  src.dataset = "20 Newsgroups";
  src.noun = "post";
  src.nouns = "posts";
  src.sample_note = "the MH-8 sample";
  src.shingle_source = "after the header block of the post is removed";
  src.shingle_sets = "the MH-8 shingle sets";
  src.archive_sha256 = ARCHIVE_SHA256;
  const auto chosen = demo::full_chosen(posts.size(), smoke);
  src.loaded = posts.size();
  src.chosen = chosen.size();
  std::vector<std::string> ids;
  for (const std::size_t p : chosen) {
    auto set = news20::shingle_text(posts[p].body);
    if (!set.empty()) {
      src.item_of.push_back(p);
      src.sets.push_back(std::move(set));
      src.names.push_back(posts[p].group + "/" + posts[p].id);
      ids.push_back(posts[p].message_id);
    }
  }
  src.external = demo::check_pairs(ids, 0, 0);  // the pairs of indexed posts that share a Message-ID
  src.external_title = "Cross-posts";
  src.external_text = "Each pair of indexed posts that share a `Message-ID` must be in one true cluster and in one LSH "
                      "cluster, at every grid point.";
  src.external_label = "cross-post pairs";
  src.external_column = "CrossPostsSplit";
  return src;
}

}  // namespace

int main(int argc, char** argv) {
  return demo::full_main(argc, argv, "20_newsgroups", load);
}

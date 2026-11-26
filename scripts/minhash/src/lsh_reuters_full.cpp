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

// MH-14c: the MH-14b check on Reuters-21578: the sketch and the LSH clusters against the exact
// Jaccard of every pair of indexed documents, at every (k, r, t) of demo::full_grid().
// demo::full_main runs the check.
// Usage: lsh_reuters_full [--smoke] <reuters21578 directory> <results directory>
// --smoke uses only the demo::SAMPLE_SIZE documents of demo::sample_indices().

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "demo/demo.hpp"
#include "news20/loader.hpp"

namespace {

namespace fs = std::filesystem;

// The SHA-256 of reuters21578.tar.gz: UCI_TAR_SHA256 in data/get_reuters.sh, which checks it.
const char* const ARCHIVE_SHA256 = "3bae43c9b14e387f76a61b6d82bf98a4fb5d3ef99ef7e7075ff2ccbcf59f9d30";
constexpr std::size_t SGM_FILES = 22;  // reut2-000.sgm to reut2-021.sgm

struct document {
  std::string name;  // "reut2-000/1": the file and the NEWID
  std::string text;
};

// Every <REUTERS> element of the 22 .sgm files, in file order, then element order.
std::vector<document> load_documents(const fs::path& dir) {
  std::vector<document> docs;
  for (std::size_t f = 0; f < SGM_FILES; ++f) {
    const std::string stem = "reut2-0" + std::string(f < 10 ? "0" : "") + std::to_string(f);
    const fs::path path = dir / (stem + ".sgm");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      throw std::runtime_error("cannot read " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (auto& d : demo::parse_reuters_sgm(text)) {
      docs.push_back(document{stem + "/" + d.newid, std::move(d.text)});
    }
  }
  return docs;
}

demo::full_source load(const fs::path& data_dir, bool smoke) {
  const auto docs = load_documents(data_dir);
  demo::full_source src;
  src.ticket = "MH-14c";
  src.stem = "lsh_reuters_full";
  src.dataset = "Reuters-21578";
  src.noun = "document";
  src.nouns = "documents";
  src.sample_note = "the demo::sample_indices sample";
  src.shingle_source = "in the `<TITLE>` and the `<BODY>` of the document, joined by a newline, with the SGML "
                       "entities decoded";
  src.shingle_sets = "the Reuters-21578 shingle sets (the decoded `<TITLE>` and `<BODY>`)";
  src.archive_sha256 = ARCHIVE_SHA256;
  const auto chosen = demo::full_chosen(docs.size(), smoke);
  src.loaded = docs.size();
  src.chosen = chosen.size();
  std::vector<std::string> keys;  // the shingle set as one string
  for (const std::size_t d : chosen) {
    auto set = news20::shingle_text(docs[d].text);
    if (!set.empty()) {
      std::string key;
      for (const auto& s : set) {
        key += s + "\n";
      }
      keys.push_back(std::move(key));
      src.item_of.push_back(d);
      src.sets.push_back(std::move(set));
      src.names.push_back(docs[d].name);
    }
  }
  src.external = demo::check_pairs(keys, 0, 0);  // the pairs of indexed documents with identical shingle sets
  src.external_title = "Identical shingle sets";
  src.external_text = "Each pair of indexed documents with identical shingle sets must be in one true cluster and in "
                      "one LSH cluster, at every grid point.";
  src.external_label = "pairs with identical shingle sets";
  src.external_column = "IdenticalSetsSplit";
  src.bands_extra =
      "![Verified rate for each J band on both datasets](lsh_reuters_full_bands_compare.svg)\n\n"
      "How to read it: the verified rate of each J band at k = 128, 32 × 4, t = 0.5, on 20 Newsgroups (blue, from "
      "`lsh_20news_full_bands.tsv`) and on Reuters-21578 (red). The dots are the measured rates, and the lines are "
      "the expected rates of each dataset. The 2 expected lines differ a little in the steep bands near t, because the "
      "mix of J values inside a band differs between the datasets. A band with fewer pairs scatters more around "
      "its line; the pair counts are in the band tables of both reports.\n\n";
  return src;
}

}  // namespace

int main(int argc, char** argv) {
  return demo::full_main(argc, argv, "reuters21578", load);
}

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

#ifndef NEWS20_LOADER_HPP_
#define NEWS20_LOADER_HPP_

// Fork-only demo code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Loader for the 20 Newsgroups dataset (version 20news-19997). The extracted
// archive has one directory per newsgroup, and one file per post.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace news20 {

/// The number of words in one shingle.
constexpr std::size_t SHINGLE_WORDS = 5;

/// One post of the dataset.
struct post {
  std::string group;  ///< the newsgroup, which is the directory name
  std::string id;     ///< the file name inside the newsgroup directory
  std::string body;   ///< the text after the header block
};

/// Drops the header block: everything before the first blank line.
/// A line that holds only "\r" counts as blank.
/// If the text has no blank line, all of it is header, and the result is empty.
std::string strip_header(const std::string& text);

/// Converts ASCII letters to lower case, and splits on every byte that is not
/// an ASCII letter or digit. Empty words are dropped.
std::vector<std::string> tokenize(const std::string& text);

/// Makes the shingles of a word list: each run of `width` consecutive words,
/// joined by single spaces. The result is sorted and has no duplicates.
/// A list with fewer than `width` words has no shingles.
std::vector<std::string> shingles(const std::vector<std::string>& words, std::size_t width = SHINGLE_WORDS);

/// Tokenizes a post body and returns its shingle set (sorted, no duplicates).
std::vector<std::string> shingle_text(const std::string& body);

/// Exact Jaccard similarity |A ∩ B| / |A ∪ B| of two sorted sets with no duplicates.
/// Two empty sets give 1, and one empty set gives 0, as in the theta Jaccard similarity.
double jaccard(const std::vector<std::string>& a, const std::vector<std::string>& b);

/// Reads every post under `root` (the extracted "20_newsgroups" directory).
/// The result is sorted by group, then by file name. Hidden entries are skipped.
/// Throws std::invalid_argument if `root` is not a directory.
std::vector<post> load_posts(const std::filesystem::path& root);

} // namespace news20

#endif // NEWS20_LOADER_HPP_

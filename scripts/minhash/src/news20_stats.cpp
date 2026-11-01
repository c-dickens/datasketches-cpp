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

// Prints the post count and the total shingle count of the 20 Newsgroups data.
// Usage: news20_stats <path to the extracted 20_newsgroups directory>

#include <cstdint>
#include <exception>
#include <iostream>

#include "news20/loader.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <20_newsgroups directory>\n";
    return 2;
  }
  try {
    const auto posts = news20::load_posts(argv[1]);
    std::uint64_t total_shingles = 0;
    std::uint64_t posts_without_shingles = 0;
    for (const auto& p : posts) {
      const auto set = news20::shingle_text(p.body);
      total_shingles += set.size();
      if (set.empty()) {
        ++posts_without_shingles;
      }
    }
    std::cout << "posts: " << posts.size() << "\n";
    std::cout << "total shingles (distinct " << news20::SHINGLE_WORDS << "-word shingles per post, summed): "
              << total_shingles << "\n";
    std::cout << "posts with no shingle (fewer than " << news20::SHINGLE_WORDS << " words): "
              << posts_without_shingles << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

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

#ifndef DEMO_WORDS_HPP_
#define DEMO_WORDS_HPP_

// Fork-only demo code (scripts/minhash/). It is C++17 and is not part of the library.
//
// The word tokenizer (lower-case letters and digits) that every dataset of the demos uses:
// 20 Newsgroups and Reuters-21578 (through news20::tokenize) and the agent trajectories (MH-15b).
// It is header-only, so that the news20 and demo libraries share it without a link cycle.

#include <string>
#include <vector>

namespace demo {

/// Converts ASCII letters to lower case, and splits on every byte that is not
/// an ASCII letter or digit. Empty words are dropped.
inline std::vector<std::string> words(const std::string& text) {
  std::vector<std::string> result;
  std::string word;
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      word.push_back(static_cast<char>(c));
    } else if (c >= 'A' && c <= 'Z') {
      word.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (!word.empty()) {
      result.push_back(word);
      word.clear();
    }
  }
  if (!word.empty()) {
    result.push_back(word);
  }
  return result;
}

}  // namespace demo

#endif  // DEMO_WORDS_HPP_

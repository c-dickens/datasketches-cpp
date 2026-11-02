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

#include "news20/loader.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace news20 {

namespace {

bool is_ascii_alnum(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

char ascii_lower(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

bool is_hidden(const std::filesystem::path& path) {
  const std::string name = path.filename().string();
  return !name.empty() && name[0] == '.';
}

} // namespace

std::string strip_header(const std::string& text) {
  std::size_t line_start = 0;
  while (line_start < text.size()) {
    std::size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;  // the last line has no newline, so it is not blank
    }
    const std::size_t length = line_end - line_start;
    if (length == 0 || (length == 1 && text[line_start] == '\r')) {
      return text.substr(line_end + 1);
    }
    line_start = line_end + 1;
  }
  return {};
}

std::vector<std::string> tokenize(const std::string& text) {
  std::vector<std::string> words;
  std::string word;
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    if (is_ascii_alnum(c)) {
      word.push_back(ascii_lower(c));
    } else if (!word.empty()) {
      words.push_back(word);
      word.clear();
    }
  }
  if (!word.empty()) {
    words.push_back(word);
  }
  return words;
}

std::vector<std::string> shingles(const std::vector<std::string>& words, std::size_t width) {
  std::vector<std::string> result;
  if (width == 0 || words.size() < width) {
    return result;
  }
  result.reserve(words.size() - width + 1);
  for (std::size_t i = 0; i + width <= words.size(); ++i) {
    std::string shingle = words[i];
    for (std::size_t j = 1; j < width; ++j) {
      shingle.push_back(' ');
      shingle += words[i + j];
    }
    result.push_back(std::move(shingle));
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::vector<std::string> shingle_text(const std::string& body) {
  return shingles(tokenize(body));
}

double jaccard(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.empty() && b.empty()) {
    return 1.0;
  }
  if (a.empty() || b.empty()) {
    return 0.0;
  }
  std::size_t common = 0;
  auto ia = a.begin();
  auto ib = b.begin();
  while (ia != a.end() && ib != b.end()) {
    if (*ia < *ib) {
      ++ia;
    } else if (*ib < *ia) {
      ++ib;
    } else {
      ++common;
      ++ia;
      ++ib;
    }
  }
  const std::size_t united = a.size() + b.size() - common;
  return static_cast<double>(common) / static_cast<double>(united);
}

std::vector<post> load_posts(const std::filesystem::path& root) {
  if (!std::filesystem::is_directory(root)) {
    throw std::invalid_argument("not a directory: " + root.string());
  }
  std::vector<std::filesystem::path> groups;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (entry.is_directory() && !is_hidden(entry.path())) {
      groups.push_back(entry.path());
    }
  }
  std::sort(groups.begin(), groups.end());

  std::vector<post> posts;
  for (const auto& group : groups) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(group)) {
      if (entry.is_regular_file() && !is_hidden(entry.path())) {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
      posts.push_back(post{group.filename().string(), file.filename().string(), strip_header(read_file(file))});
    }
  }
  return posts;
}

} // namespace news20

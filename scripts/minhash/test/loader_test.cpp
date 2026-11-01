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

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "news20/loader.hpp"

namespace fs = std::filesystem;
using strings = std::vector<std::string>;

namespace {

// Small text fixtures in the 20news-19997 layout: a header block, a blank line, a body.
const char* const POST_A =
    "Xref: cantaloupe.srv.cs.cmu.edu alt.atheism:51060 sci.space:61000\n"
    "Newsgroups: alt.atheism,sci.space\n"
    "Subject: Re: Launch window\n"
    "\n"
    "The launch window opens at dawn, and the crew is ready.\n";

// The same body, cross-posted with a different header.
const char* const POST_A_CROSSPOST =
    "Xref: cantaloupe.srv.cs.cmu.edu sci.space:61000 alt.atheism:51060\n"
    "Newsgroups: sci.space,alt.atheism\n"
    "Path: cantaloupe.srv.cs.cmu.edu!other\n"
    "\n"
    "The launch window opens at dawn, and the crew is ready.\n";

const char* const POST_B =
    "From: someone@example.org\r\n"
    "Subject: Short\r\n"
    "\r\n"
    "Too short.\r\n";

// A temporary directory that is removed at the end of the test.
struct temp_dir {
  fs::path path;
  temp_dir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("news20_test_" + std::to_string(stamp));
    fs::create_directories(path);
  }
  ~temp_dir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  temp_dir(const temp_dir&) = delete;
  temp_dir& operator=(const temp_dir&) = delete;
  void write(const std::string& relative, const std::string& text) const {
    const fs::path file = path / relative;
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
  }
};

} // namespace

TEST_CASE("strip_header drops everything before the first blank line", "[loader]") {
  REQUIRE(news20::strip_header("A: 1\nB: 2\n\nbody line\n\nsecond\n") == "body line\n\nsecond\n");
  REQUIRE(news20::strip_header(POST_B) == "Too short.\r\n");     // CRLF line ends
  REQUIRE(news20::strip_header("\nall body\n") == "all body\n"); // blank first line: empty header
  REQUIRE(news20::strip_header("A: 1\nB: 2\n").empty());         // no blank line: all header
  REQUIRE(news20::strip_header("A: 1\n\n").empty());             // header with an empty body
  REQUIRE(news20::strip_header("").empty());
}

TEST_CASE("tokenize lowers ASCII and splits on non-alphanumeric bytes", "[loader]") {
  REQUIRE(news20::tokenize("Hello, World! It's 2024") == strings{"hello", "world", "it", "s", "2024"});
  REQUIRE(news20::tokenize("  --  ").empty());
  REQUIRE(news20::tokenize("").empty());
  REQUIRE(news20::tokenize("a_b\tc\nD") == strings{"a", "b", "c", "d"});
  // Non-ASCII bytes are separators: "caf\xC3\xA9" is "café" in UTF-8.
  REQUIRE(news20::tokenize("caf\xC3\xA9 au lait") == strings{"caf", "au", "lait"});
}

TEST_CASE("shingles joins 5 consecutive words with single spaces", "[loader]") {
  const strings words{"a", "b", "c", "d", "e", "f"};
  REQUIRE(news20::shingles(words) == strings{"a b c d e", "b c d e f"});
  REQUIRE(news20::shingles(strings{"a", "b", "c", "d", "e"}) == strings{"a b c d e"});
  REQUIRE(news20::shingles(strings{"a", "b", "c", "d"}).empty());  // fewer than 5 words
  REQUIRE(news20::shingles(strings{}).empty());
  // A repeated run gives one shingle: the result is a set.
  REQUIRE(news20::shingles(strings{"x", "x", "x", "x", "x", "x", "x"}) == strings{"x x x x x"});
  // The result is sorted.
  REQUIRE(news20::shingles(strings{"z", "y", "x", "w", "v", "a"}) == strings{"y x w v a", "z y x w v"});
  REQUIRE(news20::shingles(strings{"a", "b", "c"}, 2) == strings{"a b", "b c"});
}

TEST_CASE("shingle_text tokenizes, then makes shingles", "[loader]") {
  REQUIRE(news20::shingle_text("One, two; THREE four five six.") ==
          strings{"one two three four five", "two three four five six"});
}

TEST_CASE("jaccard is exact on sorted sets", "[loader]") {
  const strings a{"p", "q", "r", "s"};
  const strings b{"r", "s", "t", "u"};
  REQUIRE(news20::jaccard(a, a) == 1.0);
  REQUIRE(news20::jaccard(a, b) == Approx(2.0 / 6.0));  // {r, s} over {p, q, r, s, t, u}
  REQUIRE(news20::jaccard(a, b) == news20::jaccard(b, a));
  REQUIRE(news20::jaccard(a, strings{"x", "y"}) == 0.0);
  REQUIRE(news20::jaccard(strings{}, strings{}) == 1.0);
  REQUIRE(news20::jaccard(a, strings{}) == 0.0);
  REQUIRE(news20::jaccard(strings{"a"}, strings{"a", "b", "c", "d"}) == Approx(0.25));
}

TEST_CASE("load_posts reads groups and posts in sorted order", "[loader]") {
  temp_dir root;
  root.write("sci.space/61000", POST_A_CROSSPOST);
  root.write("alt.atheism/51060", POST_A);
  root.write("alt.atheism/51059", POST_B);
  root.write("alt.atheism/.hidden", "ignored\n\nignored body\n");
  root.write(".hidden_group/1", "ignored\n\nignored body\n");

  const auto posts = news20::load_posts(root.path);
  REQUIRE(posts.size() == 3);
  REQUIRE(posts[0].group == "alt.atheism");
  REQUIRE(posts[0].id == "51059");
  REQUIRE(posts[0].body == "Too short.\r\n");
  REQUIRE(posts[1].group == "alt.atheism");
  REQUIRE(posts[1].id == "51060");
  REQUIRE(posts[2].group == "sci.space");
  REQUIRE(posts[2].id == "61000");

  // A cross-posted copy has the same body, so its shingle sets are identical.
  const auto a = news20::shingle_text(posts[1].body);
  const auto a_copy = news20::shingle_text(posts[2].body);
  REQUIRE(a.size() == 7);  // 11 words give 11 - 5 + 1 = 7 shingles
  REQUIRE(news20::jaccard(a, a_copy) == 1.0);

  // A post with fewer than 5 words has no shingles.
  REQUIRE(news20::shingle_text(posts[0].body).empty());
}

TEST_CASE("load_posts throws when the root is not a directory", "[loader]") {
  temp_dir root;
  REQUIRE_THROWS_AS(news20::load_posts(root.path / "missing"), std::invalid_argument);
  root.write("file", "x");
  REQUIRE_THROWS_AS(news20::load_posts(root.path / "file"), std::invalid_argument);
}

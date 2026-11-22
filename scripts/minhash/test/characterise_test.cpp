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

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "characterise/common.hpp"
#include "characterise/profiles.hpp"
#include "common_defs.hpp"
#include "minhash_lsh.hpp"
#include "minhash_sketch.hpp"

// REQUIRE(!x) instead of REQUIRE_FALSE(x): the Catch2 v2 flags of REQUIRE_FALSE make
// clang-analyzer-optin.core.EnumCastOutOfRange report a finding inside catch.hpp.

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream s(line);
  std::string field;
  while (std::getline(s, field, '\t')) {
    fields.push_back(field);
  }
  return fields;
}

// The data rows of a speed profile: the "#" lines are skipped, and the header must match.
std::vector<std::vector<std::string>> speed_rows(const std::string& tsv, const std::string& header) {
  std::istringstream lines(tsv);
  std::string line;
  std::vector<std::vector<std::string>> rows;
  bool header_seen = false;
  while (std::getline(lines, line)) {
    if (line.rfind('#', 0) == 0) {
      continue;
    }
    if (!header_seen) {
      REQUIRE(line == header);
      header_seen = true;
      continue;
    }
    rows.push_back(split_tabs(line));
  }
  REQUIRE(header_seen);
  return rows;
}

}  // namespace

TEST_CASE("quantile_header names the 9 quantile columns", "[characterise]") {
  const auto names = split_tabs(characterise::quantile_header("X"));
  REQUIRE(names.size() == 9);
  REQUIRE(names.front() == "X_Q0");
  REQUIRE(names[1] == "X_Q0.00135");
  REQUIRE(names.back() == "X_Q1");
}

TEST_CASE("write_quantiles writes min, median and max of the sketch", "[characterise]") {
  datasketches::kll_sketch<double> sketch(characterise::KLL_K);
  for (int v = 1; v <= 1000; ++v) {
    sketch.update(static_cast<double>(v));
  }
  std::ostringstream out;
  characterise::write_quantiles(out, sketch);
  const auto values = split_tabs(out.str());
  REQUIRE(values.size() == 9);
  REQUIRE(std::stod(values.front()) == 1.0);
  REQUIRE(std::stod(values.back()) == 1000.0);
  const double median = std::stod(values[4]);
  REQUIRE((median == 500.0 || median == 501.0));
}

TEST_CASE("the accuracy_j smoke profile writes a complete, deterministic TSV", "[characterise]") {
  std::ostringstream out;
  characterise::accuracy_j(out, true);
  std::istringstream lines(out.str());
  std::string line;
  std::vector<std::string> metadata;
  std::vector<std::vector<std::string>> table;  // the header, then the data rows
  while (std::getline(lines, line)) {
    if (line.rfind('#', 0) == 0) {
      metadata.push_back(line);
    } else {
      table.push_back(split_tabs(line));
    }
  }
  REQUIRE(metadata.front() == "# profile=minhash-accuracy-j-smoke");
  REQUIRE(metadata.back() == "# k_values=16,64,256");

  // 9 fixed columns, 2 x 9 quantile columns, 3 coverage columns.
  const std::size_t columns = 30;
  const std::size_t j_points = 19;
  REQUIRE(table.size() == 1 + characterise::SMOKE_K_VALUES.size() * j_points);
  REQUIRE(table.front().size() == columns);
  REQUIRE(table.front().back() == "Cover3SD");
  for (std::size_t r = 1; r < table.size(); ++r) {
    const auto& row = table[r];
    REQUIRE(row.size() == columns);
    REQUIRE(std::stoi(row[0]) == characterise::SMOKE_K_VALUES[(r - 1) / j_points]);
    REQUIRE(std::stod(row[1]) == Approx(0.05 * static_cast<double>((r - 1) % j_points + 1)));
    for (std::size_t c = columns - 3; c < columns; ++c) {
      const double coverage = std::stod(row[c]);
      REQUIRE(coverage >= 0.0);
      REQUIRE(coverage <= 1.0);
    }
  }

  std::ostringstream again;
  characterise::accuracy_j(again, true);
  REQUIRE(again.str() == out.str());
}

TEST_CASE("normalised_error divides by the theory SD", "[characterise]") {
  // sqrt(0.5 * 0.5 / 100) = 0.05, and (0.6 - 0.5) / 0.05 = 2.
  REQUIRE(characterise::normalised_error(0.6, 0.5, 100) == Approx(2.0).margin(1e-12));
  REQUIRE_THROWS_AS(characterise::normalised_error(0.5, 0.0, 100), std::invalid_argument);
  REQUIRE_THROWS_AS(characterise::normalised_error(0.5, 1.0, 100), std::invalid_argument);
}

TEST_CASE("covers is inclusive at both ends", "[characterise]") {
  const std::array<double, 3> bounds{0.25, 0.5, 0.75};
  REQUIRE(characterise::covers(bounds, 0.25));
  REQUIRE(characterise::covers(bounds, 0.5));
  REQUIRE(characterise::covers(bounds, 0.75));
  REQUIRE(!characterise::covers(bounds, 0.2499));
  REQUIRE(!characterise::covers(bounds, 0.7501));
}

TEST_CASE("num_trials interpolates log-linearly from the max to the min trials", "[characterise]") {
  REQUIRE(characterise::num_trials(1, 0, 20, 4, 16) == 65536);
  REQUIRE(characterise::num_trials(1ULL << 20, 0, 20, 4, 16) == 16);
  REQUIRE(characterise::num_trials(1ULL << 10, 0, 20, 4, 16) == 1024);
}

TEST_CASE("u64_items multiplies the index by the golden ratio constant", "[characterise]") {
  REQUIRE(characterise::u64_items(3) == std::vector<uint64_t>{0, 0x9E3779B97F4A7C15ULL, 0x3C6EF372FE94F82AULL});
}

TEST_CASE("string_items gives distinct strings of the given length", "[characterise]") {
  const auto items = characterise::string_items(2, 64);
  REQUIRE(items.size() == 2);
  REQUIRE(items[0].size() == 64);
  REQUIRE(items[1].size() == 64);
  REQUIRE(items[0] != items[1]);
  REQUIRE(characterise::string_items(1, 8) == std::vector<std::string>{std::string(8, '\0')});
}

TEST_CASE("cpu_model is not empty", "[characterise]") {
  REQUIRE(!characterise::cpu_model().empty());
}

TEST_CASE("the update_speed smoke profile writes one row per sketch, k, item type and n", "[characterise]") {
  std::ostringstream out;
  characterise::update_speed(out, true);
  REQUIRE(out.str().find("\n# cpu=") != std::string::npos);
  const auto rows =
      speed_rows(out.str(), "Sketch\tK\tItemType\tN\tTrials\tMeanNsPerItem\tMinNsPerItem\tMaxNsPerItem");
  const auto ns = characterise::pwr_2_law_points(1, 1ULL << 14, 4);
  REQUIRE(rows.size() == 2 * characterise::SMOKE_K_VALUES.size() * 3 * ns.size());
  for (const auto& row : rows) {
    REQUIRE(row.size() == 8);
    REQUIRE((row[0] == "minhash" || row[0] == "theta"));
    REQUIRE((row[2] == "u64" || row[2] == "str8" || row[2] == "str64"));
    REQUIRE(std::stoul(row[4]) == characterise::num_trials(std::stoul(row[3]), 0, 14, 2, 12));
    // A trial at small n can take less than one clock tick, so the minimum can be 0.
    REQUIRE(std::stod(row[6]) >= 0.0);
    REQUIRE(std::stod(row[5]) > 0.0);
    REQUIRE(std::stod(row[6]) <= std::stod(row[5]));
    REQUIRE(std::stod(row[5]) <= std::stod(row[7]));
  }
}

TEST_CASE("the call_speed smoke profile writes one row per sketch, k and operation", "[characterise]") {
  std::ostringstream out;
  characterise::call_speed(out, true);
  REQUIRE(out.str().find("\n# cpu=") != std::string::npos);
  const auto rows = speed_rows(out.str(), "Sketch\tK\tOperation\tCalls\tMeanNsPerCall");
  REQUIRE(rows.size() == 2 * characterise::SMOKE_K_VALUES.size() * 3);
  for (std::size_t r = 0; r < rows.size(); ++r) {
    const auto& row = rows[r];
    REQUIRE(row.size() == 5);
    REQUIRE(row[0] == (r < rows.size() / 2 ? "minhash" : "theta"));
    REQUIRE(row[2] == std::array<std::string, 3>{"construct", "merge", "jaccard"}[r % 3]);
    REQUIRE(row[3] == "1024");
    REQUIRE(std::stod(row[4]) > 0.0);
  }
}

TEST_CASE("a copy saved from the stream after i items is the sketch of [0, i)", "[characterise]") {
  const std::vector<uint64_t> sizes{1, 500, 9999};
  auto builder = datasketches::minhash_sketch::builder().set_k(16).set_seed(datasketches::DEFAULT_SEED);
  auto stream = builder.build();
  std::vector<datasketches::minhash_sketch> copies;
  for (uint64_t item = 0; item < 10000; ++item) {
    stream.update(item);
    if (copies.size() < sizes.size() && item + 1 == sizes[copies.size()]) {
      copies.push_back(stream);
    }
  }
  REQUIRE(copies.size() == sizes.size());
  for (std::size_t c = 0; c < sizes.size(); ++c) {
    auto fresh = builder.build();
    for (uint64_t item = 0; item < sizes[c]; ++item) {
      fresh.update(item);
    }
    REQUIRE(copies[c].get_signature() == fresh.get_signature());
  }
}

TEST_CASE("pwr_2_law_points gives the power-law grid from start to end", "[characterise]") {
  REQUIRE(characterise::pwr_2_law_points(2, 64, 1) == std::vector<uint64_t>{2, 4, 8, 16, 32, 64});
  // A start that is not on the grid must not skip the next grid point (4).
  REQUIRE(characterise::pwr_2_law_points(3, 64, 1) == std::vector<uint64_t>{3, 4, 8, 16, 32, 64});
  const auto points = characterise::pwr_2_law_points(2, 65536, 16);
  REQUIRE(points.front() == 2);
  REQUIRE(points.back() == 65536);
  for (std::size_t i = 1; i < points.size(); ++i) {
    REQUIRE(points[i] > points[i - 1]);
  }
  REQUIRE_THROWS_AS(characterise::pwr_2_law_points(0, 64, 1), std::invalid_argument);
  REQUIRE_THROWS_AS(characterise::pwr_2_law_points(8, 4, 1), std::invalid_argument);
  REQUIRE_THROWS_AS(characterise::pwr_2_law_points(2, 64, 0), std::invalid_argument);
}

TEST_CASE("in_b puts (n m) / 10 of the first n items in B", "[characterise]") {
  for (const uint64_t n : {1, 2, 10, 17, 1000}) {
    for (const unsigned m : {1U, 5U, 9U}) {
      uint64_t count = 0;
      for (uint64_t x = 0; x < n; ++x) {
        count += characterise::in_b(x, m) ? 1 : 0;
      }
      REQUIRE(count == n * m / 10);
    }
  }
}

TEST_CASE("theta_compact_bytes is the serialized size of the compact sketch", "[characterise]") {
  for (const uint8_t lg_k : {4, 12}) {
    for (const uint64_t n : {1, 2, 100, 10000}) {
      auto a = datasketches::update_theta_sketch::builder().set_lg_k(lg_k).build();
      for (uint64_t x = 0; x < n; ++x) {
        a.update(x);
      }
      REQUIRE(characterise::theta_compact_bytes(a) == a.compact().get_serialized_size_bytes());
    }
  }
}

TEST_CASE("the accuracy_n_half smoke profile writes a complete, deterministic TSV", "[characterise]") {
  std::ostringstream out;
  characterise::accuracy_n_half(out, true);
  std::istringstream lines(out.str());
  std::string line;
  std::vector<std::string> metadata;
  std::vector<std::vector<std::string>> table;  // the header, then the data rows
  while (std::getline(lines, line)) {
    if (line.rfind('#', 0) == 0) {
      metadata.push_back(line);
    } else {
      table.push_back(split_tabs(line));
    }
  }
  REQUIRE(metadata.front() == "# profile=minhash-accuracy-n-half-smoke");
  REQUIRE(metadata.back() == "# theta_lg_k=log2(K)-1");

  // 8 fixed columns, 9 MinHash quantiles, 3 MinHash coverages, 2 theta columns, 9 theta
  // quantiles, theta coverage, 4 theta size columns and MH_Bytes.
  const std::size_t columns = 37;
  REQUIRE(table.front().size() == columns);
  REQUIRE(table.front().front() == "K");
  REQUIRE(table.front().back() == "MH_Bytes");

  // One row for each (k, m, n) with 0 < (n m) / 10 < n.
  std::size_t rows_per_k = 0;
  for (const unsigned m : {1U, 5U, 9U}) {
    for (const uint64_t n : characterise::pwr_2_law_points(2, 4096, 4)) {
      rows_per_k += (n * m / 10 > 0 && n * m / 10 < n) ? 1 : 0;
    }
  }
  REQUIRE(table.size() == 1 + 2 * rows_per_k);
  for (std::size_t r = 1; r < table.size(); ++r) {
    REQUIRE(table[r].size() == columns);
    REQUIRE(table[r][0] == (r <= rows_per_k ? "64" : "256"));
  }

  std::ostringstream again;
  characterise::accuracy_n_half(again, true);
  REQUIRE(again.str() == out.str());
}

TEST_CASE("lsh_candidate is true when the keys are equal in at least one band", "[characterise]") {
  REQUIRE(characterise::lsh_candidate({1, 2, 3}, {4, 2, 6}));
  REQUIRE(!characterise::lsh_candidate({1, 2, 3}, {4, 5, 6}));
  REQUIRE_THROWS_AS(characterise::lsh_candidate({1, 2}, {1, 2, 3}), std::invalid_argument);
}

TEST_CASE("lsh_theory_rate is 1 - (1 - j^r)^b", "[characterise]") {
  // 1 - (1 - 0.5^4)^32 = 1 - 0.9375^32
  REQUIRE(characterise::lsh_theory_rate(0.5, 4, 32) == Approx(0.873211).margin(1e-6));
  REQUIRE(characterise::lsh_theory_rate(0.0, 4, 32) == 0.0);
  REQUIRE(characterise::lsh_theory_rate(1.0, 4, 32) == 1.0);
  // Small j^r: 1 - (1 - 0.75^64)^4 = 4.03627587217052671e-8 (60-digit decimal). The direct form
  // 1 - (1 - x)^b cancels and is wrong in the 9th digit here.
  REQUIRE(characterise::lsh_theory_rate(0.75, 64, 4) == Approx(4.03627587217052671e-8).epsilon(1e-12));
}

TEST_CASE("the lsh_accuracy smoke profile writes a complete, deterministic TSV", "[characterise]") {
  std::ostringstream out;
  characterise::lsh_accuracy(out, true);
  std::istringstream lines(out.str());
  std::string line;
  std::vector<std::string> metadata;
  std::vector<std::vector<std::string>> table;  // the header, then the data rows
  while (std::getline(lines, line)) {
    if (line.rfind('#', 0) == 0) {
      metadata.push_back(line);
    } else {
      table.push_back(split_tabs(line));
    }
  }
  REQUIRE(metadata.front() == "# profile=minhash-lsh-accuracy-smoke");
  REQUIRE(metadata[metadata.size() - 2] == "# k_values=64,128");
  REQUIRE(metadata.back() == "# thresholds=0.5,0.7,0.9");
  REQUIRE(table.front() == std::vector<std::string>{"K", "B", "R", "TrueJ", "Trials", "CandidateRate", "TheoryRate",
                                                    "CandidateZ", "ReportedRate_t0.5", "ReportedRate_t0.7",
                                                    "ReportedRate_t0.9"});

  // One row for each (k, b, J): k = 64, 128; b = 1, 2, ..., k; J = j / 20, j = 0 ... 20.
  std::size_t r = 1;
  for (const unsigned k : {64U, 128U}) {
    for (unsigned b = 1; b <= k; b *= 2) {
      for (unsigned j = 0; j <= 20; ++j, ++r) {
        REQUIRE(r < table.size());
        const auto& row = table[r];
        REQUIRE(row.size() == table.front().size());
        REQUIRE(std::stoul(row[0]) == k);
        REQUIRE(std::stoul(row[1]) == b);
        REQUIRE(std::stoul(row[2]) == k / b);
        REQUIRE(std::stod(row[3]) == Approx(j / 20.0).margin(1e-12));
        REQUIRE(row[4] == "256");
        // J = 0 is never a candidate, and J = 1 (identical sketches) always is.
        if (j == 0) {
          REQUIRE(row[5] == "0");
        }
        if (j == 20) {
          REQUIRE(row[5] == "1");
        }
      }
    }
  }
  REQUIRE(r == table.size());  // 315 data rows: (7 + 8) b values x 21 J points

  std::ostringstream again;
  characterise::lsh_accuracy(again, true);
  REQUIRE(again.str() == out.str());
}

TEST_CASE("lsh accuracy: the index finds the band-key candidates", "[characterise]") {
  using index = datasketches::minhash_lsh_index;
  constexpr uint64_t unrelated = 150;
  constexpr uint64_t documents = 200;
  constexpr uint64_t group_size = 5;
  std::vector<datasketches::minhash_sketch> sketches;
  for (uint64_t d = 0; d < documents; ++d) {
    auto s = datasketches::minhash_sketch::builder().set_k(128).set_seed(datasketches::DEFAULT_SEED).build();
    // Documents 0 to 149: [1000 d, 1000 d + 100). Documents 150 to 199: 10 groups of 5. Member m of
    // group g is document g with the items [1000 g + 10 m, + 10) moved to [10^9 + 1000 g + 10 m, + 10).
    const bool member = d >= unrelated;
    const uint64_t base = member ? (d - unrelated) / group_size : d;
    const uint64_t moved = 1000 * base + (member ? 10 * ((d - unrelated) % group_size) : 0);
    for (uint64_t item = 1000 * base; item < 1000 * base + 100; ++item) {
      const bool is_moved = member && item >= moved && item < moved + 10;
      s.update(is_moved ? 1000000000 + item : item);
    }
    sketches.push_back(s);
  }

  for (const uint16_t b : {16, 32}) {
    index lsh(b, 0.5);
    for (uint64_t d = 0; d < documents; ++d) {
      lsh.add(d, sketches[d]);
    }
    uint64_t pairs = 0;  // candidates other than the document itself
    for (uint64_t d = 0; d < documents; ++d) {
      const auto keys_d = index::band_keys(sketches[d], b);
      std::vector<uint64_t> expected;
      for (uint64_t e = 0; e < documents; ++e) {
        if (characterise::lsh_candidate(keys_d, index::band_keys(sketches[e], b))) {
          expected.push_back(e);
        }
      }
      REQUIRE(lsh.query_candidates(sketches[d]) == expected);
      pairs += expected.size() - 1;
    }
    REQUIRE(pairs > 0);  // the groups give candidates, so the check is not trivial
  }
}

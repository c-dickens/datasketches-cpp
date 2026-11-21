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

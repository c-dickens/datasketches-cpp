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

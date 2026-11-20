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

#ifndef CHARACTERISE_COMMON_HPP_
#define CHARACTERISE_COMMON_HPP_

// Fork-only characterisation code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Shared helpers of the characterisation profiles: the constants, the TSV metadata and the
// quantile columns, in the style of apache/datasketches-characterization.

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "kll_sketch.hpp"

namespace characterise {

constexpr std::array<double, 9> QUANTILES = {0.0, 0.00135, 0.02275, 0.15866, 0.5, 0.84134, 0.97725, 0.99865, 1.0};
constexpr std::array<uint16_t, 5> K_VALUES = {16, 64, 256, 1024, 4096};
constexpr std::array<uint16_t, 3> SMOKE_K_VALUES = {16, 64, 256};
constexpr uint16_t KLL_K = 10000;

/// Writes "# profile=<name>", "# git_commit=<sha>", "# compiler=<version>" and
/// "# build_flags=<flags>", one per line. The commit and the flags come from compile
/// definitions that CMake sets (git rev-parse HEAD at configure time, and
/// CMAKE_CXX_FLAGS plus the flags of the build type).
void write_metadata(std::ostream& out, const std::string& profile);

/// "<prefix>_Q0\t<prefix>_Q0.00135\t...\t<prefix>_Q1", one name for each of QUANTILES.
std::string quantile_header(const std::string& prefix);

/// The 9 quantiles of the sketch at QUANTILES, tab-separated, with no leading or trailing tab.
void write_quantiles(std::ostream& out, const datasketches::kll_sketch<double>& sketch);

/// (estimate - j) / sqrt(j (1 - j) / k). Throws std::invalid_argument unless 0 < j < 1.
double normalised_error(double estimate, double j, uint16_t k);

/// True when bounds[0] <= j <= bounds[2].
bool covers(const std::array<double, 3>& bounds, double j);

/// The points start, then next = max(cur + 1, round(2^(log2(cur) + 1/ppo))), up to and
/// including end. This is pwr_2_law_next of the characterisation repo.
/// Throws std::invalid_argument if start is 0, end < start, or ppo is 0.
std::vector<uint64_t> pwr_2_law_points(uint64_t start, uint64_t end, unsigned ppo);

/// round(2^lg_t), with lg_t = lg_max_t - (lg_max_t - lg_min_t) * (log2(x) - lg_min_x) / (lg_max_x - lg_min_x),
/// and lg_t limited to [lg_min_t, lg_max_t]. This is get_num_trials of the characterisation repo.
uint64_t num_trials(uint64_t x, unsigned lg_min_x, unsigned lg_max_x, unsigned lg_min_t, unsigned lg_max_t);

/// The CPU model: on macOS from sysctlbyname("machdep.cpu.brand_string"), on Linux from the
/// first "model name" line of /proc/cpuinfo, otherwise "unknown".
std::string cpu_model();

/// The items of one type, made once: "u64" gives i * 0x9E3779B97F4A7C15 (mod 2^64) as uint64_t;
/// "str8" and "str64" give strings of 8 and 64 bytes: the 8 little-endian bytes of i, then
/// 'x' up to the length.
std::vector<uint64_t> u64_items(std::size_t n);
std::vector<std::string> string_items(std::size_t n, std::size_t length);

}  // namespace characterise

#endif  // CHARACTERISE_COMMON_HPP_

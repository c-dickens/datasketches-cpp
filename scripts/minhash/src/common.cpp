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

#include "characterise/common.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace characterise {

void write_metadata(std::ostream& out, const std::string& profile) {
  out << "# profile=" << profile << "\n";
  out << "# git_commit=" << CHARACTERISE_GIT_COMMIT << "\n";
  out << "# compiler=" << CHARACTERISE_COMPILER << "\n";
  out << "# build_flags=" << CHARACTERISE_BUILD_FLAGS << "\n";
}

std::string quantile_header(const std::string& prefix) {
  std::ostringstream s;
  for (std::size_t q = 0; q < QUANTILES.size(); ++q) {
    s << (q == 0 ? "" : "\t") << prefix << "_Q" << QUANTILES[q];
  }
  return s.str();
}

void write_quantiles(std::ostream& out, const datasketches::kll_sketch<double>& sketch) {
  for (std::size_t q = 0; q < QUANTILES.size(); ++q) {
    out << (q == 0 ? "" : "\t") << sketch.get_quantile(QUANTILES[q]);
  }
}

double normalised_error(double estimate, double j, uint16_t k) {
  if (!(j > 0.0 && j < 1.0)) {
    throw std::invalid_argument("normalised_error: j must be in (0, 1)");
  }
  return (estimate - j) / std::sqrt(j * (1.0 - j) / k);
}

bool covers(const std::array<double, 3>& bounds, double j) {
  return bounds[0] <= j && j <= bounds[2];
}

std::vector<uint64_t> pwr_2_law_points(uint64_t start, uint64_t end, unsigned ppo) {
  if (start == 0 || end < start || ppo == 0) {
    throw std::invalid_argument("pwr_2_law_points: need 0 < start <= end and ppo > 0");
  }
  std::vector<uint64_t> points;
  for (uint64_t cur = start; cur < end;) {
    points.push_back(cur);
    const double next = std::round(std::exp2(std::log2(static_cast<double>(cur)) + 1.0 / ppo));
    cur = std::max(cur + 1, static_cast<uint64_t>(next));
  }
  // The rounding drifts off the powers of 2, so the last point is end itself.
  points.push_back(end);
  return points;
}

uint64_t num_trials(uint64_t x, unsigned lg_min_x, unsigned lg_max_x, unsigned lg_min_t, unsigned lg_max_t) {
  const double slope = (static_cast<double>(lg_max_t) - lg_min_t) / (static_cast<double>(lg_max_x) - lg_min_x);
  const double lg_t = lg_max_t - slope * (std::log2(static_cast<double>(x)) - lg_min_x);
  return static_cast<uint64_t>(std::round(std::exp2(std::clamp<double>(lg_t, lg_min_t, lg_max_t))));
}

std::string cpu_model() {
#if defined(__APPLE__)
  char brand[256] = {};
  std::size_t size = sizeof(brand);
  if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0 && brand[0] != '\0') {
    return brand;
  }
#elif defined(__linux__)
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    const auto colon = line.find(':');
    if (line.rfind("model name", 0) == 0 && colon != std::string::npos && colon + 2 <= line.size()) {
      return line.substr(colon + 2);
    }
  }
#endif
  return "unknown";
}

std::vector<uint64_t> u64_items(std::size_t n) {
  std::vector<uint64_t> items(n);
  for (std::size_t i = 0; i < n; ++i) {
    items[i] = static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
  }
  return items;
}

std::vector<std::string> string_items(std::size_t n, std::size_t length) {
  std::vector<std::string> items(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::string s(8, '\0');
    for (std::size_t b = 0; b < 8; ++b) {
      s[b] = static_cast<char>((static_cast<uint64_t>(i) >> (8 * b)) & 0xFF);
    }
    s.resize(length, 'x');
    items[i] = std::move(s);
  }
  return items;
}

}  // namespace characterise

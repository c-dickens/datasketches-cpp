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

#include <cmath>
#include <sstream>
#include <stdexcept>

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
  std::vector<uint64_t> points{start};
  for (uint64_t cur = start;;) {
    // pwr_2_law_next: the next point of the grid round(2^(i / ppo)) that is above cur. Start at
    // floor, not round: every i <= floor(ppo log2(cur)) gives a point <= cur, so no point is skipped.
    auto i = static_cast<int64_t>(std::floor(std::log2(static_cast<double>(cur)) * ppo));
    uint64_t next = 0;
    do {
      next = static_cast<uint64_t>(std::round(std::pow(2.0, static_cast<double>(++i) / ppo)));
    } while (next <= cur);
    if (next > end) {
      break;
    }
    points.push_back(next);
    cur = next;
  }
  return points;
}

bool in_b(uint64_t x, unsigned m) {
  return (x + 1) * m / 10 > x * m / 10;
}

uint64_t theta_compact_bytes(const datasketches::update_theta_sketch& a) {
  const uint64_t r = a.get_num_retained();
  if (a.is_estimation_mode()) {
    return 24 + 8 * r;
  }
  return r == 1 ? 16 : 16 + 8 * r;
}

bool lsh_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b) {
  if (keys_a.size() != keys_b.size()) {
    throw std::invalid_argument("lsh_candidate: the key lists differ in size");
  }
  for (std::size_t band = 0; band < keys_a.size(); ++band) {
    if (keys_a[band] == keys_b[band]) {
      return true;
    }
  }
  return false;
}

double lsh_theory_rate(double j, unsigned r, unsigned b) {
  return 1.0 - std::pow(1.0 - std::pow(j, r), b);
}

}  // namespace characterise

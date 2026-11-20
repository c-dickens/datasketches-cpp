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

}  // namespace characterise

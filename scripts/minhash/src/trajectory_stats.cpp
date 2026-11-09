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

// Prints the trajectory count and the minimum, median and maximum token-set
// size of a trajectory file.
// Usage: trajectory_stats <path to data/trajectories_<N>.jsonl>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

#include "trajectory/featurizer.hpp"

namespace {

// The median of a sorted, non-empty list: the middle value, or the mean of the two middle values.
double median(const std::vector<std::size_t>& sorted) {
  const std::size_t n = sorted.size();
  if (n % 2 == 1) {
    return static_cast<double>(sorted[n / 2]);
  }
  return (static_cast<double>(sorted[n / 2 - 1]) + static_cast<double>(sorted[n / 2])) / 2.0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <trajectories_N.jsonl>\n";
    return 2;
  }
  try {
    const auto all = trajectory::load_trajectories(argv[1]);
    if (all.empty()) {
      std::cerr << "error: no trajectory in " << argv[1] << "\n";
      return 1;
    }
    std::vector<std::size_t> sizes;
    std::vector<std::size_t> step_counts;
    std::uint64_t bash_steps = 0;
    std::uint64_t bash_unknown = 0;
    std::uint64_t empty_sets = 0;
    for (const auto& r : all) {
      const auto set = trajectory::tokens(r.steps);
      sizes.push_back(set.size());
      step_counts.push_back(r.steps.size());
      if (set.empty()) {
        ++empty_sets;
      }
      for (const auto& s : r.steps) {
        if (s.tool == "execute_bash") {
          ++bash_steps;
          if (s.outcome == "unknown") {
            ++bash_unknown;
          }
        }
      }
    }
    std::sort(sizes.begin(), sizes.end());
    std::sort(step_counts.begin(), step_counts.end());
    std::cout << "trajectories: " << all.size() << "\n";
    std::cout << "steps per trajectory: min " << step_counts.front() << ", median " << median(step_counts)
              << ", max " << step_counts.back() << "\n";
    std::cout << "token-set size (distinct " << trajectory::TOKEN_STEPS << "-step tokens): min " << sizes.front()
              << ", median " << median(sizes) << ", max " << sizes.back() << "\n";
    std::cout << "empty token sets: " << empty_sets << "\n";
    std::cout << "bash steps with outcome unknown (footer cut off): " << bash_unknown << " of " << bash_steps << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

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

// Runs one characterisation profile, and writes its TSV to stdout.
// Usage: minhash_characterise <profile-name>
// A profile with a second TSV (minhash-lsh-performance) writes it to the file that the
// environment variable CHARACTERISE_QUALITY_OUT names.

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>

#include "characterise/profiles.hpp"

namespace {

// The full results are committed, so each TSV starts with the ASF licence header, as
// tools/rat-check.sh requires. The lines start with "#", so readers skip them as comments.
const char* const LICENCE_HEADER =
    "# Licensed to the Apache Software Foundation (ASF) under one\n"
    "# or more contributor license agreements.  See the NOTICE file\n"
    "# distributed with this work for additional information\n"
    "# regarding copyright ownership.  The ASF licenses this file\n"
    "# to you under the Apache License, Version 2.0 (the\n"
    "# \"License\"); you may not use this file except in compliance\n"
    "# with the License.  You may obtain a copy of the License at\n"
    "#\n"
    "#   http://www.apache.org/licenses/LICENSE-2.0\n"
    "#\n"
    "# Unless required by applicable law or agreed to in writing,\n"
    "# software distributed under the License is distributed on an\n"
    "# \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY\n"
    "# KIND, either express or implied.  See the License for the\n"
    "# specific language governing permissions and limitations\n"
    "# under the License.\n";

struct profile {
  const char* name;
  void (*run)(std::ostream& out, bool smoke);
  bool smoke;
  // For a profile with a second TSV, in place of run.
  void (*run_two)(std::ostream& out, std::ostream& second_out, bool smoke) = nullptr;
};

const char* const QUALITY_OUT_VARIABLE = "CHARACTERISE_QUALITY_OUT";

// The registry. Each profile has a full version and a "-smoke" version.
const profile PROFILES[] = {
    {"minhash-accuracy-j", characterise::accuracy_j, false},
    {"minhash-accuracy-j-smoke", characterise::accuracy_j, true},
    {"minhash-accuracy-n", characterise::accuracy_n, false},
    {"minhash-accuracy-n-smoke", characterise::accuracy_n, true},
    {"minhash-accuracy-n-half", characterise::accuracy_n_half, false},
    {"minhash-accuracy-n-half-smoke", characterise::accuracy_n_half, true},
    {"minhash-lsh-accuracy", characterise::lsh_accuracy, false},
    {"minhash-lsh-accuracy-smoke", characterise::lsh_accuracy, true},
    {"minhash-update-speed", characterise::update_speed, false},
    {"minhash-update-speed-smoke", characterise::update_speed, true},
    {"minhash-call-speed", characterise::call_speed, false},
    {"minhash-call-speed-smoke", characterise::call_speed, true},
    {"minhash-lsh-performance", nullptr, false, characterise::lsh_performance},
    {"minhash-lsh-performance-smoke", nullptr, true, characterise::lsh_performance},
};

int usage(const char* program) {
  std::cerr << "usage: " << program << " <profile-name>\nprofiles:\n";
  for (const auto& p : PROFILES) {
    std::cerr << "  " << p.name << "\n";
  }
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return usage(argv[0]);
  }
  const std::string name = argv[1];
  for (const auto& p : PROFILES) {
    if (name == p.name) {
      std::ofstream second_out;
      if (p.run_two != nullptr) {
        const char* path = std::getenv(QUALITY_OUT_VARIABLE);  // NOLINT(concurrency-mt-unsafe): single-threaded
        if (path == nullptr || path[0] == '\0') {
          std::cerr << "error: set " << QUALITY_OUT_VARIABLE << " to the file for the quality TSV of " << p.name
                    << "\n";
          return 2;
        }
        second_out.open(path);
        if (!second_out) {
          std::cerr << "error: cannot open " << path << "\n";
          return 1;
        }
      }
      try {
        std::cout << LICENCE_HEADER;
        if (p.run_two != nullptr) {
          second_out << LICENCE_HEADER;
          p.run_two(std::cout, second_out, p.smoke);
          second_out.flush();
          if (!second_out) {
            std::cerr << "error: cannot write the quality TSV\n";
            return 1;
          }
        } else {
          p.run(std::cout, p.smoke);
        }
        std::cout.flush();
        if (!std::cout) {
          std::cerr << "error: cannot write the TSV to stdout\n";
          return 1;
        }
        return 0;
      } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
      }
    }
  }
  return usage(argv[0]);
}

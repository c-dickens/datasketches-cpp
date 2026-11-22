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

#ifndef CHARACTERISE_PROFILES_HPP_
#define CHARACTERISE_PROFILES_HPP_

// Fork-only characterisation code (scripts/minhash/). It is C++17 and is not part of the library.
//
// One function for each profile. Each writes its TSV to `out`, and its progress to stderr.
// `smoke` selects the small, fast version of the profile.

#include <ostream>

namespace characterise {

/// The error of the MinHash Jaccard estimate against the true J (MH-16).
void accuracy_j(std::ostream& out, bool smoke);

/// The error of the MinHash and theta Jaccard estimates against the union size n (MH-17).
void accuracy_n(std::ostream& out, bool smoke);

/// The same, with theta at half the nominal k, lg_k = log2(k) - 1, so that theta never uses
/// more memory than MinHash (MH-17).
void accuracy_n_half(std::ostream& out, bool smoke);

/// The LSH candidate rate against the theory 1 - (1 - J^r)^b, and the reported rate
/// (candidate and estimated J >= t), for every power-of-2 b that divides k (MH-19).
void lsh_accuracy(std::ostream& out, bool smoke);

/// The update time of MinHash and theta per item, against n (MH-18).
void update_speed(std::ostream& out, bool smoke);

/// The time of construction, merge and jaccard of MinHash and theta per call, against k (MH-18).
void call_speed(std::ostream& out, bool smoke);

}  // namespace characterise

#endif  // CHARACTERISE_PROFILES_HPP_

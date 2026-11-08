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

#include <cmath>

#include <catch2/catch.hpp>

#include <test_allocator.hpp>

#include "minhash_sketch.hpp"

namespace datasketches {

TEST_CASE("minhash sketch: empty", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().build();
  REQUIRE(sketch.is_empty());
  REQUIRE(sketch.get_num_hashes() == minhash_constants::DEFAULT_K);
  REQUIRE(sketch.get_seed() == DEFAULT_SEED);
  REQUIRE(sketch.get_signature().size() == minhash_constants::DEFAULT_K);
  for (const uint64_t slot: sketch.get_signature()) {
    REQUIRE(slot == std::numeric_limits<uint64_t>::max());
  }

  // two empty sketches represent equal (empty) sets
  auto other = minhash_sketch::builder().build();
  REQUIRE(sketch.jaccard(other) == 1.0);
  REQUIRE(sketch.jaccard_bounds(other) == std::array<double, 3>{1.0, 1.0, 1.0});

  // empty vs non-empty
  other.update(static_cast<uint64_t>(1));
  REQUIRE(!other.is_empty());
  REQUIRE(sketch.jaccard(other) == 0.0);
  REQUIRE(other.jaccard(sketch) == 0.0);
  REQUIRE(sketch.jaccard_bounds(other) == std::array<double, 3>{0.0, 0.0, 0.0});
  REQUIRE(other.jaccard_bounds(sketch) == std::array<double, 3>{0.0, 0.0, 0.0});
}

TEST_CASE("minhash sketch: builder validation and defaults", "[minhash_sketch]") {
  REQUIRE_THROWS_AS(minhash_sketch::builder().set_k(0), std::invalid_argument);

  // k does not have to be a power of 2
  auto sketch = minhash_sketch::builder().set_k(112).set_seed(123).build();
  REQUIRE(sketch.get_num_hashes() == 112);
  REQUIRE(sketch.get_seed() == 123);

  auto tiny = minhash_sketch::builder().set_k(1).build();
  REQUIRE(tiny.get_num_hashes() == 1);
}

TEST_CASE("minhash sketch: update populates every slot", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().set_k(1024).build();
  sketch.update(static_cast<uint64_t>(42));
  for (const uint64_t slot: sketch.get_signature()) {
    REQUIRE(slot != std::numeric_limits<uint64_t>::max());
  }
}

TEST_CASE("minhash sketch: seed affects the signature", "[minhash_sketch]") {
  auto sketch_a = minhash_sketch::builder().set_k(64).build();
  auto sketch_b = minhash_sketch::builder().set_k(64).set_seed(123).build();
  sketch_a.update(static_cast<uint64_t>(1));
  sketch_b.update(static_cast<uint64_t>(1));
  REQUIRE(sketch_a.get_signature() != sketch_b.get_signature());
}

TEST_CASE("minhash sketch: identical streams", "[minhash_sketch]") {
  auto sketch_a = minhash_sketch::builder().build();
  auto sketch_b = minhash_sketch::builder().build();
  for (uint64_t i = 0; i < 1000; ++i) {
    sketch_a.update(i);
    sketch_b.update(i);
  }
  REQUIRE(sketch_a.get_signature() == sketch_b.get_signature());
  REQUIRE(sketch_a.jaccard(sketch_b) == 1.0);

  // bounds at the matches == k extreme: estimate and upper bound pin to 1
  const auto bounds = sketch_a.jaccard_bounds(sketch_b);
  REQUIRE(bounds[0] > 0.9);
  REQUIRE(bounds[0] < 1.0);
  REQUIRE(bounds[1] == 1.0);
  REQUIRE(bounds[2] == 1.0);
}

TEST_CASE("minhash sketch: disjoint streams", "[minhash_sketch]") {
  const uint16_t k = 1024;
  auto sketch_a = minhash_sketch::builder().set_k(k).build();
  auto sketch_b = minhash_sketch::builder().set_k(k).build();
  for (uint64_t i = 0; i < 10000; ++i) sketch_a.update(i);
  for (uint64_t i = 10000; i < 20000; ++i) sketch_b.update(i);
  // true J = 0, estimate should be within ~3 standard deviations of 0
  REQUIRE(sketch_a.jaccard(sketch_b) < 3.0 / std::sqrt(static_cast<double>(k)));

  // bounds at the matches == 0 extreme: lower bound and estimate pin to 0
  const auto bounds = sketch_a.jaccard_bounds(sketch_b);
  REQUIRE(bounds[0] == 0.0);
  REQUIRE(bounds[1] == 0.0);
  REQUIRE(bounds[2] > 0.0);
  REQUIRE(bounds[2] < 0.02);
}

TEST_CASE("minhash sketch: known overlap", "[minhash_sketch]") {
  const uint16_t k = 2048;
  auto sketch_a = minhash_sketch::builder().set_k(k).build();
  auto sketch_b = minhash_sketch::builder().set_k(k).build();
  // A = [0, 15000), B = [0, 10000) + [15000, 20000): intersection 10000, union 20000, J = 0.5
  for (uint64_t i = 0; i < 15000; ++i) sketch_a.update(i);
  for (uint64_t i = 0; i < 10000; ++i) sketch_b.update(i);
  for (uint64_t i = 15000; i < 20000; ++i) sketch_b.update(i);

  const double true_jaccard = 0.5;
  const double estimate = sketch_a.jaccard(sketch_b);
  REQUIRE(estimate == Approx(true_jaccard).margin(3 * std::sqrt(0.25 / k)));

  const auto bounds = sketch_a.jaccard_bounds(sketch_b, 3);
  REQUIRE(bounds[0] <= true_jaccard);
  REQUIRE(bounds[2] >= true_jaccard);
  REQUIRE(bounds[0] <= bounds[1]);
  REQUIRE(bounds[1] <= bounds[2]);
  REQUIRE(bounds[1] == estimate);
}

TEST_CASE("minhash sketch: merge equals sketch of union", "[minhash_sketch]") {
  auto sketch_a = minhash_sketch::builder().set_k(512).build();
  auto sketch_b = minhash_sketch::builder().set_k(512).build();
  auto sketch_union = minhash_sketch::builder().set_k(512).build();
  for (uint64_t i = 0; i < 3000; ++i) {
    sketch_a.update(i);
    sketch_union.update(i);
  }
  for (uint64_t i = 2000; i < 5000; ++i) {
    sketch_b.update(i);
    sketch_union.update(i);
  }
  sketch_a.merge(sketch_b);
  REQUIRE(sketch_a.get_signature() == sketch_union.get_signature());
  REQUIRE(!sketch_a.is_empty());

  // merging an empty sketch is a no-op; merging into an empty sketch copies
  auto empty = minhash_sketch::builder().set_k(512).build();
  auto before = sketch_union.get_signature();
  sketch_union.merge(empty);
  REQUIRE(sketch_union.get_signature() == before);
  REQUIRE(!sketch_union.is_empty());
  empty.merge(sketch_union);
  REQUIRE(empty.get_signature() == before);
  REQUIRE(!empty.is_empty());

  // merging two empty sketches stays empty, preserving empty-pair jaccard semantics
  auto empty_a = minhash_sketch::builder().set_k(512).build();
  auto empty_b = minhash_sketch::builder().set_k(512).build();
  empty_a.merge(empty_b);
  REQUIRE(empty_a.is_empty());
  REQUIRE(empty_a.jaccard(empty_b) == 1.0);
}

TEST_CASE("minhash sketch: incompatible k or seed throws", "[minhash_sketch]") {
  auto sketch_a = minhash_sketch::builder().set_k(256).build();
  auto sketch_b = minhash_sketch::builder().set_k(128).build();
  auto sketch_c = minhash_sketch::builder().set_k(256).set_seed(123).build();
  REQUIRE_THROWS_AS(sketch_a.merge(sketch_b), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard(sketch_b), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.merge(sketch_c), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard(sketch_c), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_c), std::invalid_argument);
}

TEST_CASE("minhash sketch: cross-type update consistency", "[minhash_sketch]") {
  auto make = [] { return minhash_sketch::builder().set_k(64).build(); };

  // all integral types canonicalize to the same item
  auto s_u64 = make(); s_u64.update(static_cast<uint64_t>(1));
  auto s_i64 = make(); s_i64.update(static_cast<int64_t>(1));
  auto s_u32 = make(); s_u32.update(static_cast<uint32_t>(1));
  auto s_i32 = make(); s_i32.update(static_cast<int32_t>(1));
  auto s_u16 = make(); s_u16.update(static_cast<uint16_t>(1));
  auto s_i16 = make(); s_i16.update(static_cast<int16_t>(1));
  auto s_u8 = make(); s_u8.update(static_cast<uint8_t>(1));
  auto s_i8 = make(); s_i8.update(static_cast<int8_t>(1));
  REQUIRE(s_u64.get_signature() == s_i64.get_signature());
  REQUIRE(s_u64.get_signature() == s_u32.get_signature());
  REQUIRE(s_u64.get_signature() == s_i32.get_signature());
  REQUIRE(s_u64.get_signature() == s_u16.get_signature());
  REQUIRE(s_u64.get_signature() == s_i16.get_signature());
  REQUIRE(s_u64.get_signature() == s_u8.get_signature());
  REQUIRE(s_u64.get_signature() == s_i8.get_signature());

  // a plain int literal must compile and match
  auto s_int = make(); s_int.update(1);
  REQUIRE(s_u64.get_signature() == s_int.get_signature());

  // float and double canonicalize to the same item; -0.0 == 0.0
  auto s_d = make(); s_d.update(1.0);
  auto s_f = make(); s_f.update(1.0f);
  REQUIRE(s_d.get_signature() == s_f.get_signature());
  auto s_zn = make(); s_zn.update(-0.0);
  auto s_zp = make(); s_zp.update(0.0);
  REQUIRE(s_zn.get_signature() == s_zp.get_signature());

  // every NaN bit pattern canonicalizes to Java's Double.doubleToLongBits value
  auto s_qnan = make(); s_qnan.update(std::numeric_limits<double>::quiet_NaN());
  auto s_pnan = make(); s_pnan.update(std::nan("1"));
  auto s_fnan = make(); s_fnan.update(std::numeric_limits<float>::quiet_NaN());
  REQUIRE(s_qnan.get_signature() == s_pnan.get_signature());
  REQUIRE(s_qnan.get_signature() == s_fnan.get_signature());

  // doubles are canonicalized by bit pattern, so 1.0 is a different item than 1
  // (deliberate, matches the theta sketch and the Java implementation)
  REQUIRE(s_d.get_signature() != s_u64.get_signature());

  // negative values lock the sign-extension chain (u32 -> i32 -> i64 and narrower)
  auto n_i64 = make(); n_i64.update(static_cast<int64_t>(-123));
  auto n_i32 = make(); n_i32.update(static_cast<int32_t>(-123));
  auto n_i16 = make(); n_i16.update(static_cast<int16_t>(-123));
  auto n_i8 = make(); n_i8.update(static_cast<int8_t>(-123));
  REQUIRE(n_i64.get_signature() == n_i32.get_signature());
  REQUIRE(n_i64.get_signature() == n_i16.get_signature());
  REQUIRE(n_i64.get_signature() == n_i8.get_signature());
  // unsigned values reinterpret as signed before widening, matching the Java chain
  auto n_u32 = make(); n_u32.update(static_cast<uint32_t>(0xFFFFFFFF));
  auto n_neg1 = make(); n_neg1.update(static_cast<int64_t>(-1));
  REQUIRE(n_u32.get_signature() == n_neg1.get_signature());

  // empty string is a no-op
  auto s_str = make();
  s_str.update(std::string());
  REQUIRE(s_str.is_empty());
  s_str.update(std::string("minhash"));
  REQUIRE(!s_str.is_empty());
}

using minhash_sketch_test_alloc = minhash_sketch_alloc<test_allocator<uint64_t>>;

TEST_CASE("minhash sketch: allocator", "[minhash_sketch]") {
  test_allocator_total_bytes = 0;
  test_allocator_net_allocations = 0;
  {
    auto sketch = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(300).build();
    for (uint64_t i = 0; i < 100; ++i) sketch.update(i);
    auto copy = sketch;
    copy.merge(sketch);
    REQUIRE(copy.jaccard(sketch) == 1.0);
    auto str = sketch.to_string();
    REQUIRE(str.length() > 0);
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

} /* namespace datasketches */

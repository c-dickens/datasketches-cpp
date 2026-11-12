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

TEST_CASE("minhash sketch: disjoint streams", "[minhash_sketch][minhash][stats]") {
  const uint16_t k = 1024;
  auto sketch_a = minhash_sketch::builder().set_k(k).build();
  auto sketch_b = minhash_sketch::builder().set_k(k).build();
  for (uint64_t i = 0; i < 10000; ++i) { sketch_a.update(i); }
  for (uint64_t i = 10000; i < 20000; ++i) { sketch_b.update(i); }
  // True J = 0, so matches ~ Binomial(k, 0) and SD = 0: a slot can match only if
  // two different items give the same 64-bit slot value. By the union bound,
  // P(any match) <= k * |A| * |B| / 2^64 = 1024 * 10^8 / 2^64 < 6e-9.
  REQUIRE(sketch_a.jaccard(sketch_b) == 0.0);

  // Bounds at matches == 0: the lower bound and the estimate are 0. The upper
  // bound is the exact Clopper-Pearson bound for 0 successes in k trials, which
  // solves (1 - p)^k = delta, so p = 1 - delta^(1/k), with delta = Phi(-2) for
  // 2 SD. The library's erf (Abramowitz-Stegun 7.1.28) has |error| <= 3e-7, so
  // delta has |error| <= 1.5e-7. Since p ~ -ln(delta) / k, the relative error of
  // p is at most (1.5e-7 / delta) / |ln(delta)|, about 2e-6. Allow 1e-5.
  const auto bounds = sketch_a.jaccard_bounds(sketch_b, 2);
  const double delta = 0.5 * std::erfc(2.0 / std::sqrt(2.0));
  REQUIRE(bounds[0] == 0.0);
  REQUIRE(bounds[1] == 0.0);
  REQUIRE(bounds[2] == Approx(1.0 - std::pow(delta, 1.0 / k)).epsilon(1e-5));
}

TEST_CASE("minhash sketch: known overlap", "[minhash_sketch][minhash][stats]") {
  const uint16_t k = 2048;
  auto sketch_a = minhash_sketch::builder().set_k(k).build();
  auto sketch_b = minhash_sketch::builder().set_k(k).build();
  // A = [0, 15000), B = [0, 10000) + [15000, 20000): intersection 10000, union 20000, J = 0.5
  for (uint64_t i = 0; i < 15000; ++i) { sketch_a.update(i); }
  for (uint64_t i = 0; i < 10000; ++i) { sketch_b.update(i); }
  for (uint64_t i = 15000; i < 20000; ++i) { sketch_b.update(i); }

  // matches ~ Binomial(k, J), so the estimate matches / k has
  // SD = sqrt(J * (1 - J) / k). Require |estimate - J| <= 5 * SD.
  const double true_jaccard = 0.5;
  const double sd = std::sqrt(true_jaccard * (1 - true_jaccard) / k);
  const double estimate = sketch_a.jaccard(sketch_b);
  REQUIRE(estimate == Approx(true_jaccard).margin(5 * sd));

  // The 3 SD bounds are approximate Clopper-Pearson bounds on matches / k.
  // For p = matches / k, they lie within z^2 / k of the normal bounds
  // p -/+ z * sqrt(p * (1 - p) / k), with z = 3. This checks that the sketch
  // passes k, matches and num_std_devs correctly. It does not check coverage:
  // one fixed sample cannot do that without a chance of failure.
  const auto bounds = sketch_a.jaccard_bounds(sketch_b, 3);
  const double z = 3;
  const double half_width = z * std::sqrt(estimate * (1 - estimate) / k);
  REQUIRE(bounds[1] == estimate);
  REQUIRE(bounds[0] == Approx(estimate - half_width).margin(z * z / k));
  REQUIRE(bounds[2] == Approx(estimate + half_width).margin(z * z / k));
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

TEST_CASE("minhash sketch: jaccard_bounds num_std_devs must be 1, 2 or 3", "[minhash_sketch]") {
  auto sketch_a = minhash_sketch::builder().set_k(64).build();
  auto sketch_b = minhash_sketch::builder().set_k(64).build();
  // the range is checked before the empty cases
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b, 4), std::invalid_argument);
  sketch_a.update(static_cast<uint64_t>(1));
  sketch_b.update(static_cast<uint64_t>(2));
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(sketch_a.jaccard_bounds(sketch_b, 255), std::invalid_argument);
  for (uint8_t num_std_devs = 1; num_std_devs <= 3; ++num_std_devs) {
    REQUIRE_NOTHROW(sketch_a.jaccard_bounds(sketch_b, num_std_devs));
  }
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
    for (uint64_t i = 0; i < 100; ++i) { sketch.update(i); }
    auto copy = sketch;
    copy.merge(sketch);
    REQUIRE(copy.jaccard(sketch) == 1.0);
    auto str = sketch.to_string();
    REQUIRE(str.length() > 0);
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

TEST_CASE("minhash sketch: copy and move", "[minhash_sketch]") {
  test_allocator_total_bytes = 0;
  test_allocator_net_allocations = 0;
  {
    auto sketch = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(100).set_seed(123).build();
    for (uint64_t i = 0; i < 1000; ++i) { sketch.update(i); }
    const auto signature = sketch.get_signature();

    // copy construction
    minhash_sketch_test_alloc copy(sketch);
    REQUIRE(copy.get_signature() == signature);
    REQUIRE(copy.get_seed() == 123);
    REQUIRE(!copy.is_empty());

    // move construction
    minhash_sketch_test_alloc moved(std::move(copy));
    REQUIRE(moved.get_signature() == signature);
    REQUIRE(moved.get_seed() == 123);
    REQUIRE(!moved.is_empty());

    // copy assignment over an empty sketch with different k and seed
    auto assigned = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(10).build();
    assigned = sketch;
    REQUIRE(assigned.get_signature() == signature);
    REQUIRE(assigned.get_seed() == 123);
    REQUIRE(!assigned.is_empty());

    // move assignment over an empty sketch with different k and seed
    auto move_assigned = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(10).build();
    move_assigned = std::move(assigned);
    REQUIRE(move_assigned.get_signature() == signature);
    REQUIRE(move_assigned.get_seed() == 123);
    REQUIRE(!move_assigned.is_empty());

    // the empty flag is copied too
    auto empty = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(100).set_seed(123).build();
    move_assigned = empty;
    REQUIRE(move_assigned.is_empty());
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

TEST_CASE("minhash sketch: seed hash", "[minhash_sketch]") {
  // 0x93CC is the seed hash of DEFAULT_SEED (9001) in every DataSketches language
  REQUIRE(minhash_sketch::builder().build().get_seed_hash() == 0x93CC);
  REQUIRE(minhash_sketch::builder().build().get_seed_hash() == compute_seed_hash(DEFAULT_SEED));
  REQUIRE(minhash_sketch::builder().set_seed(123).build().get_seed_hash() == compute_seed_hash(123));
  REQUIRE(minhash_sketch::builder().set_seed(123).build().get_seed_hash() != 0x93CC);
}

TEST_CASE("minhash sketch: k = 1", "[minhash_sketch]") {
  auto make = [] { return minhash_sketch::builder().set_k(1).build(); };
  // delta = Phi(-2) for 2 SD; the library's erf has |error| <= 3e-7, so delta has |error| <= 1.5e-7
  const double delta = 0.5 * std::erfc(2.0 / std::sqrt(2.0));

  // identical sets: 1 match in 1 slot. Exact Clopper-Pearson lower bound for
  // n successes in n trials solves p^n = delta, so p = delta for n = 1.
  auto a = make(); a.update(static_cast<uint64_t>(1));
  auto b = make(); b.update(static_cast<uint64_t>(1));
  REQUIRE(a.get_signature().size() == 1);
  REQUIRE(a.jaccard(b) == 1.0);
  auto bounds = a.jaccard_bounds(b);
  REQUIRE(bounds[0] == Approx(delta).margin(1e-6));
  REQUIRE(bounds[1] == 1.0);
  REQUIRE(bounds[2] == 1.0);

  // different items: 0 matches in 1 slot (barring a 64-bit tie). Exact upper
  // bound for 0 successes in n trials solves (1 - p)^n = delta, so p = 1 - delta.
  auto c = make(); c.update(static_cast<uint64_t>(2));
  REQUIRE(a.jaccard(c) == 0.0);
  bounds = a.jaccard_bounds(c);
  REQUIRE(bounds[0] == 0.0);
  REQUIRE(bounds[1] == 0.0);
  REQUIRE(bounds[2] == Approx(1.0 - delta).margin(1e-6));

  // merge equals the sketch of the union
  auto u = make(); u.update(static_cast<uint64_t>(1)); u.update(static_cast<uint64_t>(2));
  a.merge(c);
  REQUIRE(a.get_signature() == u.get_signature());
}

TEST_CASE("minhash sketch: merge does not depend on the order", "[minhash_sketch]") {
  auto make = [] { return minhash_sketch::builder().set_k(128).build(); };
  auto a = make(); for (uint64_t i = 0; i < 1000; ++i) { a.update(i); }
  auto b = make(); for (uint64_t i = 500; i < 1500; ++i) { b.update(i); }
  auto c = make(); for (uint64_t i = 1200; i < 3000; ++i) { c.update(i); }

  // commutative: a + b == b + a
  auto ab = a; ab.merge(b);
  auto ba = b; ba.merge(a);
  REQUIRE(ab.get_signature() == ba.get_signature());

  // associative: (a + b) + c == a + (b + c)
  auto ab_c = ab; ab_c.merge(c);
  auto bc = b; bc.merge(c);
  auto a_bc = a; a_bc.merge(bc);
  REQUIRE(ab_c.get_signature() == a_bc.get_signature());

  // self-merge changes nothing
  auto aa = a; aa.merge(aa);
  REQUIRE(aa.get_signature() == a.get_signature());
}

TEST_CASE("minhash sketch: maximum values", "[minhash_sketch]") {
  // largest k
  auto big = minhash_sketch::builder().set_k(65535).build();
  REQUIRE(big.get_num_hashes() == 65535);
  big.update(static_cast<uint64_t>(1));
  REQUIRE(big.jaccard(big) == 1.0);
  const auto& signature = big.get_signature();
  REQUIRE(std::count(signature.begin(), signature.end(), std::numeric_limits<uint64_t>::max()) == 0);

  // extreme items: UINT64_MAX has the same 8 bytes as int64 -1, so it is the same item
  auto make = [] { return minhash_sketch::builder().set_k(64).build(); };
  auto u_max = make(); u_max.update(std::numeric_limits<uint64_t>::max());
  auto i_neg1 = make(); i_neg1.update(static_cast<int64_t>(-1));
  auto i_min = make(); i_min.update(std::numeric_limits<int64_t>::min());
  auto i_max = make(); i_max.update(std::numeric_limits<int64_t>::max());
  REQUIRE(!u_max.is_empty());
  REQUIRE(!i_min.is_empty());
  REQUIRE(!i_max.is_empty());
  REQUIRE(u_max.get_signature() == i_neg1.get_signature());
  REQUIRE(i_min.get_signature() != i_max.get_signature());
  REQUIRE(i_min.get_signature() != u_max.get_signature());
}

} /* namespace datasketches */

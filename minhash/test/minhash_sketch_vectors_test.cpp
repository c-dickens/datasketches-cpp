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

// Test vectors for minhash/SPEC.md, at k = 8 and seed = DEFAULT_SEED (9001).
//
// Each expected signature is checked two ways, and neither way uses the sketch:
// 1. a direct XXHash64::hash call on bytes built by hand, then the SplitMix64
//    loop written out below from ADR-0002;
// 2. fixed values computed with the Python xxhash package (an independent XXH64)
//    and the same loop in Python. The same values are in minhash/SPEC.md.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

#include "minhash_sketch.hpp"
#include "xxhash64.h"

namespace datasketches {

namespace {

constexpr uint16_t K = 8;
using bytes_t = std::vector<unsigned char>;
using slots_t = std::vector<uint64_t>;

// SplitMix64 (Steele, Lea and Flood; Vigna's splitmix64.c), written out from ADR-0002
uint64_t splitmix64_next(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// the signature of a sketch that received only this item
slots_t expected_slots(const bytes_t& bytes) {
  uint64_t state = XXHash64::hash(bytes.data(), bytes.size(), DEFAULT_SEED);
  slots_t slots(K);
  for (uint64_t& slot: slots) { slot = splitmix64_next(state) >> 1; }
  return slots;
}

slots_t signature_of(const minhash_sketch& sketch) {
  return {sketch.get_signature().begin(), sketch.get_signature().end()};
}

minhash_sketch make() { return minhash_sketch::builder().set_k(K).build(); }

struct vector_case {
  const char* name;
  bytes_t bytes;     // the item's bytes, built by hand
  uint64_t xxh64;    // XXH64(bytes, 9001), from Python xxhash
  slots_t slots;     // the k = 8 slot values, from Python
};

const std::vector<vector_case>& cases() {
  static const std::vector<vector_case> c = {
    {"int64 0", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0xD71D64972584A288ULL,
      {0x63994435A96EBAE5ULL, 0x3B617E1A355C0F92ULL, 0x47FB59AD21DC0D5CULL, 0x681DBA163E1F7EEDULL,
       0x01AD99477E4DC78DULL, 0x07EFEC7F1A4C5232ULL, 0x4C3C0D0CED1E2E37ULL, 0x6F31AD0D99A092A9ULL}},
    {"int64 1", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0xEFE8454A6499F311ULL,
      {0x3FB4D85440219E25ULL, 0x015603DA2C01CB17ULL, 0x672B154DF53262B7ULL, 0x170466E675D595DBULL,
       0x6EAB3E2E008876BAULL, 0x6E39555F027B5919ULL, 0x0C717A2D4559CE4AULL, 0x0535E083277429EFULL}},
    {"int64 -1", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 0xCCBB58D2567CAC5CULL,
      {0x7A62DDE66704545FULL, 0x63B0887375FA2546ULL, 0x6128CA04C0D92999ULL, 0x24267F23D2D8DB0DULL,
       0x638A6AF59157DF85ULL, 0x22B2ADDCAD6FDE01ULL, 0x05B6EA3DB6AA85B0ULL, 0x2D2C1C3D2CED017CULL}},
    {"int64 INT64_MIN", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}, 0x2ABDB38C33D3914DULL,
      {0x06837D7659609CDBULL, 0x6DD0320E8CA2CE42ULL, 0x504912CA49C094CFULL, 0x3B70403B416501BBULL,
       0x195A683D112A51F6ULL, 0x07C9EDF949BEC243ULL, 0x57B4C522FE8C5A1DULL, 0x6AD0F1243C22098CULL}},
    {"int64 INT64_MAX", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}, 0x7ECC0ACD93A2C6C7ULL,
      {0x3EF877423B7584ACULL, 0x4FFAC9F4E7DEA686ULL, 0x76DC0B9EE0D90CC7ULL, 0x2B4AAE8BE4B92067ULL,
       0x0292E5559BB22DB7ULL, 0x7D6CCA34BDD6CA1AULL, 0x545E42EC0DA8BF78ULL, 0x465A220E2F9BE341ULL}},
    {"double NaN", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F}, 0x0AF4A7FC74C26FA5ULL,
      {0x7BDA394405A04F5EULL, 0x58DD75AC4B59BFC2ULL, 0x2959AD051A3FF774ULL, 0x46C203DA750D0EE6ULL,
       0x68A7A0D1FC406B3CULL, 0x7D691DC23D1B329BULL, 0x08440ABBF28542CAULL, 0x63F236F7534795F4ULL}},
    {"double 1.5", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F}, 0x5225F787BBE7CDB8ULL,
      {0x78A0E05187FDD0B3ULL, 0x32E8A587ECD51A3CULL, 0x03264B41F00A5A96ULL, 0x20BB71885E50190FULL,
       0x12099A528A44ED60ULL, 0x31B633B413601D67ULL, 0x06564CB5252C3953ULL, 0x36B7B62E42B4744AULL}},
    {"string a", {0x61}, 0x0DFA4919E8948650ULL,
      {0x76F3106D751773CDULL, 0x044635EAEF4D88B1ULL, 0x7B8D147CEB7E83F3ULL, 0x2EEC005CD443811DULL,
       0x2B0EC77D91DE4751ULL, 0x60C61DC03F829647ULL, 0x0C556477EEA17559ULL, 0x77182CA4FD70DD0CULL}},
    {"string MinHash", {0x4D, 0x69, 0x6E, 0x48, 0x61, 0x73, 0x68}, 0xEF891A4DC448511BULL,
      {0x03879EEC4C4BF67BULL, 0x2601A647F7FEDD91ULL, 0x2DF46719E7E35E59ULL, 0x41294A5018E2F528ULL,
       0x01AD5B7F829283C2ULL, 0x70BDDE88ABBEC31DULL, 0x5E7C6CD564439D76ULL, 0x488EEFE85EEC3F3EULL}},
    {"string naive with i-diaeresis (UTF-8)", {0x6E, 0x61, 0xC3, 0xAF, 0x76, 0x65}, 0x2FC4FEEC7B91A894ULL,
      {0x2BAB49E4316B5E82ULL, 0x08BAF3D8DEBAD5B2ULL, 0x442F11B08137763AULL, 0x22A11B18E0B461DBULL,
       0x7FD760A870286500ULL, 0x4C58E56854994DE5ULL, 0x309B3D148DF76EFAULL, 0x6A49322A9722B549ULL}},
  };
  return c;
}

const vector_case& find(const std::string& name) {
  for (const auto& c: cases()) {
    if (name == c.name) { return c; }
  }
  throw std::invalid_argument("no test vector named " + name);
}

} // namespace

TEST_CASE("minhash vectors: SplitMix64 reference outputs", "[minhash][vectors]") {
  // from s = 0, the reference generator (Vigna's splitmix64.c) gives these three values
  uint64_t state = 0;
  REQUIRE(splitmix64_next(state) == 0xE220A8397B1DCDAFULL);
  REQUIRE(splitmix64_next(state) == 0x6E789E6AA1B965F4ULL);
  REQUIRE(splitmix64_next(state) == 0x06C45D188009454FULL);
}

TEST_CASE("minhash vectors: XXH64 and the slot formula match the fixed values", "[minhash][vectors]") {
  for (const auto& c: cases()) {
    INFO(c.name);
    REQUIRE(XXHash64::hash(c.bytes.data(), c.bytes.size(), DEFAULT_SEED) == c.xxh64);
    REQUIRE(expected_slots(c.bytes) == c.slots);
    for (const uint64_t slot: c.slots) { REQUIRE(slot <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())); }
  }
}

TEST_CASE("minhash vectors: integers", "[minhash][vectors]") {
  struct int_case { const char* name; int64_t value; };
  const int_case ints[] = {
    {"int64 0", 0}, {"int64 1", 1}, {"int64 -1", -1},
    {"int64 INT64_MIN", std::numeric_limits<int64_t>::min()},
    {"int64 INT64_MAX", std::numeric_limits<int64_t>::max()},
  };
  for (const auto& c: ints) {
    INFO(c.name);
    auto sketch = make();
    sketch.update(c.value);
    REQUIRE(signature_of(sketch) == find(c.name).slots);
  }

  // int32 -1, uint32 0xFFFFFFFF and uint8 255 all equal int64 -1
  const slots_t& minus_one = find("int64 -1").slots;
  auto s_i32 = make(); s_i32.update(static_cast<int32_t>(-1));
  auto s_u32 = make(); s_u32.update(static_cast<uint32_t>(0xFFFFFFFF));
  auto s_u8 = make(); s_u8.update(static_cast<uint8_t>(255));
  auto s_u64 = make(); s_u64.update(std::numeric_limits<uint64_t>::max());
  REQUIRE(signature_of(s_i32) == minus_one);
  REQUIRE(signature_of(s_u32) == minus_one);
  REQUIRE(signature_of(s_u8) == minus_one);
  REQUIRE(signature_of(s_u64) == minus_one);
}

TEST_CASE("minhash vectors: doubles", "[minhash][vectors]") {
  // 0.0 and -0.0 are the same item, and their bits are those of int64 0
  auto s_pos = make(); s_pos.update(0.0);
  auto s_neg = make(); s_neg.update(-0.0);
  REQUIRE(signature_of(s_pos) == find("int64 0").slots);
  REQUIRE(signature_of(s_neg) == find("int64 0").slots);

  // every NaN becomes 0x7FF8000000000000
  auto s_nan = make(); s_nan.update(std::numeric_limits<double>::quiet_NaN());
  REQUIRE(signature_of(s_nan) == find("double NaN").slots);

  // 1.5 is 0x3FF8000000000000
  auto s_15 = make(); s_15.update(1.5);
  REQUIRE(signature_of(s_15) == find("double 1.5").slots);
  auto s_15f = make(); s_15f.update(1.5f);
  REQUIRE(signature_of(s_15f) == find("double 1.5").slots);
}

TEST_CASE("minhash vectors: strings", "[minhash][vectors]") {
  auto s_a = make(); s_a.update(std::string("a"));
  REQUIRE(signature_of(s_a) == find("string a").slots);
  auto s_minhash = make(); s_minhash.update(std::string("MinHash"));
  REQUIRE(signature_of(s_minhash) == find("string MinHash").slots);
  auto s_naive = make(); s_naive.update(std::string("na\xC3\xAFve"));
  REQUIRE(signature_of(s_naive) == find("string naive with i-diaeresis (UTF-8)").slots);

  // an empty string is ignored
  auto s_empty = make(); s_empty.update(std::string());
  REQUIRE(s_empty.is_empty());
  REQUIRE(signature_of(s_empty) == slots_t(K, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
}

TEST_CASE("minhash vectors: several items keep the slot-wise minimum", "[minhash][vectors]") {
  auto sketch = make();
  sketch.update(static_cast<int64_t>(1));
  sketch.update(std::string("a"));
  sketch.update(1.5);
  const slots_t& a = find("int64 1").slots;
  const slots_t& b = find("string a").slots;
  const slots_t& c = find("double 1.5").slots;
  slots_t expected(K);
  for (size_t i = 0; i < K; ++i) { expected[i] = std::min(a[i], std::min(b[i], c[i])); }
  REQUIRE(signature_of(sketch) == expected);
}

} /* namespace datasketches */

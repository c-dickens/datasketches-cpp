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

// Serialized format, version 1 (minhash/SPEC.md). The expected bytes are written
// out by hand from the slot values in SPEC.md ("Test vectors"), in little-endian
// order, with family ID 24 and the seed hash 0x93CC of seed 9001. They do not come
// from the implementation.

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

#include <test_allocator.hpp>

#include "minhash_sketch.hpp"

namespace datasketches {

namespace {

using bytes_t = std::vector<uint8_t>;

const uint64_t SEED = 9001;

// empty sketch, k = 8: the preamble only
const bytes_t EMPTY_K8 = {
  0x01, 0x01, 0x18, 0x01, 0x08, 0x00, 0xCC, 0x93,
};

// "int64 0", k = 8
const bytes_t INT64_0_K8 = {
  0x01, 0x01, 0x18, 0x00, 0x08, 0x00, 0xCC, 0x93,
  0xE5, 0xBA, 0x6E, 0xA9, 0x35, 0x44, 0x99, 0x63,  // 63994435A96EBAE5
  0x92, 0x0F, 0x5C, 0x35, 0x1A, 0x7E, 0x61, 0x3B,  // 3B617E1A355C0F92
  0x5C, 0x0D, 0xDC, 0x21, 0xAD, 0x59, 0xFB, 0x47,  // 47FB59AD21DC0D5C
  0xED, 0x7E, 0x1F, 0x3E, 0x16, 0xBA, 0x1D, 0x68,  // 681DBA163E1F7EED
  0x8D, 0xC7, 0x4D, 0x7E, 0x47, 0x99, 0xAD, 0x01,  // 01AD99477E4DC78D
  0x32, 0x52, 0x4C, 0x1A, 0x7F, 0xEC, 0xEF, 0x07,  // 07EFEC7F1A4C5232
  0x37, 0x2E, 0x1E, 0xED, 0x0C, 0x0D, 0x3C, 0x4C,  // 4C3C0D0CED1E2E37
  0xA9, 0x92, 0xA0, 0x99, 0x0D, 0xAD, 0x31, 0x6F,  // 6F31AD0D99A092A9
};

const std::vector<uint64_t> INT64_0_SLOTS = {
  0x63994435A96EBAE5ULL, 0x3B617E1A355C0F92ULL, 0x47FB59AD21DC0D5CULL, 0x681DBA163E1F7EEDULL,
  0x01AD99477E4DC78DULL, 0x07EFEC7F1A4C5232ULL, 0x4C3C0D0CED1E2E37ULL, 0x6F31AD0D99A092A9ULL,
};

// string "a", k = 1
const bytes_t STRING_A_K1 = {
  0x01, 0x01, 0x18, 0x00, 0x01, 0x00, 0xCC, 0x93,
  0xCD, 0x73, 0x17, 0x75, 0x6D, 0x10, 0xF3, 0x76,  // 76F3106D751773CD
};

bytes_t serialize_bytes(const minhash_sketch& sketch) {
  const auto bytes = sketch.serialize();
  return {bytes.begin(), bytes.end()};
}

bytes_t serialize_stream(const minhash_sketch& sketch) {
  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  sketch.serialize(s);
  const std::string str = s.str();
  return {str.begin(), str.end()};
}

minhash_sketch deserialize_stream(const bytes_t& bytes, uint64_t seed = SEED) {
  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  s.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return minhash_sketch::deserialize(s, seed);
}

minhash_sketch deserialize_bytes(const bytes_t& bytes, uint64_t seed = SEED) {
  return minhash_sketch::deserialize(bytes.data(), bytes.size(), seed);
}

void check_same(const minhash_sketch& a, const minhash_sketch& b) {
  REQUIRE(a.get_num_hashes() == b.get_num_hashes());
  REQUIRE(a.get_seed() == b.get_seed());
  REQUIRE(a.get_seed_hash() == b.get_seed_hash());
  REQUIRE(a.is_empty() == b.is_empty());
  REQUIRE(a.get_signature() == b.get_signature());
}

// Both APIs must reject the buffer with this exception type.
template<typename E>
void check_rejected(const bytes_t& bytes) {
  REQUIRE_THROWS_AS(deserialize_bytes(bytes), E);
  REQUIRE_THROWS_AS(deserialize_stream(bytes), E);
}

} // namespace

TEST_CASE("minhash serde vectors: empty sketch, k = 8", "[minhash_sketch_serde]") {
  const auto sketch = minhash_sketch::builder().set_k(8).set_seed(SEED).build();
  REQUIRE(sketch.get_serialized_size_bytes() == 8);
  REQUIRE(serialize_bytes(sketch) == EMPTY_K8);
  REQUIRE(serialize_stream(sketch) == EMPTY_K8);
  for (const auto& d: {deserialize_bytes(EMPTY_K8), deserialize_stream(EMPTY_K8)}) {
    REQUIRE(d.is_empty());
    REQUIRE(d.get_num_hashes() == 8);
    REQUIRE(d.get_seed_hash() == 0x93CC);
    REQUIRE(d.get_signature() == std::vector<uint64_t>(8, minhash_constants::EMPTY_MARKER));
  }
}

TEST_CASE("minhash serde vectors: int64 0, k = 8", "[minhash_sketch_serde]") {
  auto sketch = minhash_sketch::builder().set_k(8).set_seed(SEED).build();
  sketch.update(static_cast<int64_t>(0));
  REQUIRE(sketch.get_serialized_size_bytes() == 72);
  REQUIRE(serialize_bytes(sketch) == INT64_0_K8);
  REQUIRE(serialize_stream(sketch) == INT64_0_K8);
  for (const auto& d: {deserialize_bytes(INT64_0_K8), deserialize_stream(INT64_0_K8)}) {
    REQUIRE_FALSE(d.is_empty());
    REQUIRE(d.get_num_hashes() == 8);
    REQUIRE(d.get_seed_hash() == 0x93CC);
    REQUIRE(d.get_signature() == INT64_0_SLOTS);
  }
}

TEST_CASE("minhash serde vectors: string a, k = 1", "[minhash_sketch_serde]") {
  auto sketch = minhash_sketch::builder().set_k(1).set_seed(SEED).build();
  sketch.update(std::string("a"));
  REQUIRE(sketch.get_serialized_size_bytes() == 16);
  REQUIRE(serialize_bytes(sketch) == STRING_A_K1);
  REQUIRE(serialize_stream(sketch) == STRING_A_K1);
  for (const auto& d: {deserialize_bytes(STRING_A_K1), deserialize_stream(STRING_A_K1)}) {
    REQUIRE_FALSE(d.is_empty());
    REQUIRE(d.get_num_hashes() == 1);
    REQUIRE(d.get_seed_hash() == 0x93CC);
    REQUIRE(d.get_signature() == std::vector<uint64_t>{0x76F3106D751773CDULL});
  }
}

TEST_CASE("minhash serde: round trips", "[minhash_sketch_serde]") {
  for (const uint16_t k: {static_cast<uint16_t>(1), static_cast<uint16_t>(256), static_cast<uint16_t>(65535)}) {
    auto a = minhash_sketch::builder().set_k(k).set_seed(123).build();
    auto b = minhash_sketch::builder().set_k(k).set_seed(123).build();
    for (uint64_t i = 0; i < 1000; ++i) {
      a.update(i);
      b.update(i + 500);
    }
    const auto bytes = serialize_bytes(a);
    REQUIRE(bytes.size() == a.get_serialized_size_bytes());
    REQUIRE(bytes.size() == 8 + 8 * static_cast<size_t>(k));
    REQUIRE(serialize_stream(a) == bytes);
    for (const auto& d: {deserialize_bytes(bytes, 123), deserialize_stream(bytes, 123)}) {
      check_same(a, d);
      REQUIRE(d.jaccard(b) == a.jaccard(b));
      REQUIRE(d.jaccard(a) == 1.0);
      auto merged_d = d;
      merged_d.merge(b);
      auto merged_a = a;
      merged_a.merge(b);
      check_same(merged_a, merged_d);
      REQUIRE(serialize_bytes(d) == bytes);
    }
  }
}

TEST_CASE("minhash serde: empty round trip keeps k and merges", "[minhash_sketch_serde]") {
  const auto empty = minhash_sketch::builder().set_k(100).set_seed(123).build();
  const auto bytes = serialize_bytes(empty);
  REQUIRE(bytes.size() == 8);
  auto other = minhash_sketch::builder().set_k(100).set_seed(123).build();
  other.update(std::string("x"));
  for (auto d: {deserialize_bytes(bytes, 123), deserialize_stream(bytes, 123)}) {
    check_same(empty, d);
    REQUIRE(d.jaccard(empty) == 1.0);
    REQUIRE(d.jaccard(other) == 0.0);
    d.merge(other);
    check_same(other, d);
  }
}

TEST_CASE("minhash serde: header_size_bytes", "[minhash_sketch_serde]") {
  auto sketch = minhash_sketch::builder().set_k(16).build();
  sketch.update(42);
  const unsigned header = 13;
  const auto with_header = sketch.serialize(header);
  const auto plain = sketch.serialize();
  REQUIRE(with_header.size() == header + plain.size());
  for (unsigned i = 0; i < header; ++i) { REQUIRE(with_header[i] == 0); }
  REQUIRE(bytes_t(with_header.begin() + header, with_header.end()) == bytes_t(plain.begin(), plain.end()));
  const auto d = minhash_sketch::deserialize(with_header.data() + header, with_header.size() - header);
  check_same(sketch, d);
}

TEST_CASE("minhash serde: slot values at INT64_MAX in a non-empty sketch", "[minhash_sketch_serde]") {
  // Only the flag decides emptiness, so a non-empty sketch may hold the empty marker in a slot.
  bytes_t bytes = {0x01, 0x01, 0x18, 0x00, 0x02, 0x00, 0xCC, 0x93};
  for (int slot = 0; slot < 2; ++slot) {
    for (int i = 0; i < 7; ++i) { bytes.push_back(0xFF); }
    bytes.push_back(0x7F);
  }
  for (const auto& d: {deserialize_bytes(bytes), deserialize_stream(bytes)}) {
    REQUIRE_FALSE(d.is_empty());
    REQUIRE(d.get_signature() == std::vector<uint64_t>(2, minhash_constants::EMPTY_MARKER));
    REQUIRE(serialize_bytes(d) == bytes);
    REQUIRE(serialize_stream(d) == bytes);
    auto other = minhash_sketch::builder().set_k(2).build();
    other.update(1);
    auto merged = d;
    merged.merge(other);
    REQUIRE(merged.get_signature() == other.get_signature());
    REQUIRE(d.jaccard(d) == 1.0);
  }
}

TEST_CASE("minhash serde: extra bytes after the sketch are ignored", "[minhash_sketch_serde]") {
  bytes_t bytes = STRING_A_K1;
  bytes.push_back(0xAB);
  bytes.push_back(0xCD);
  check_same(deserialize_bytes(bytes), deserialize_bytes(STRING_A_K1));
  bytes_t empty = EMPTY_K8;
  empty.push_back(0xFF);
  check_same(deserialize_bytes(empty), deserialize_bytes(EMPTY_K8));
}

TEST_CASE("minhash serde: invalid buffers", "[minhash_sketch_serde]") {
  SECTION("size less than 8 bytes") {
    const bytes_t bytes(EMPTY_K8.begin(), EMPTY_K8.begin() + 7);
    REQUIRE_THROWS_AS(deserialize_bytes(bytes), std::invalid_argument);
    REQUIRE_THROWS_AS(minhash_sketch::deserialize(EMPTY_K8.data(), 0), std::invalid_argument);
  }
  SECTION("preamble longs") {
    bytes_t bytes = INT64_0_K8;
    bytes[0] = 2;
    check_rejected<std::invalid_argument>(bytes);
  }
  SECTION("serial version") {
    bytes_t bytes = INT64_0_K8;
    bytes[1] = 2;
    check_rejected<std::invalid_argument>(bytes);
  }
  SECTION("family ID") {
    bytes_t bytes = INT64_0_K8;
    bytes[2] = 3;  // theta
    check_rejected<std::invalid_argument>(bytes);
  }
  SECTION("hash-list flag") {
    bytes_t bytes = INT64_0_K8;
    bytes[3] = 0x02;
    check_rejected<std::invalid_argument>(bytes);
    try {
      deserialize_bytes(bytes);
      FAIL("no exception");
    } catch (const std::invalid_argument& e) {
      REQUIRE(std::string(e.what()).find("hash-list form is not supported by format version 1") != std::string::npos);
    }
  }
  SECTION("unassigned flag bits") {
    for (int bit = 2; bit < 8; ++bit) {
      bytes_t bytes = INT64_0_K8;
      bytes[3] = static_cast<uint8_t>(1U << static_cast<unsigned>(bit));
      check_rejected<std::invalid_argument>(bytes);
    }
  }
  SECTION("k = 0") {
    bytes_t bytes = EMPTY_K8;
    bytes[4] = 0;
    check_rejected<std::invalid_argument>(bytes);
  }
  SECTION("seed hash") {
    bytes_t bytes = INT64_0_K8;
    bytes[6] ^= 0x01;
    check_rejected<std::invalid_argument>(bytes);
    // the right bytes with the wrong seed
    REQUIRE_THROWS_AS(deserialize_bytes(INT64_0_K8, 123), std::invalid_argument);
    REQUIRE_THROWS_AS(deserialize_stream(INT64_0_K8, 123), std::invalid_argument);
  }
  SECTION("size less than 8 + 8k bytes") {
    const bytes_t bytes(INT64_0_K8.begin(), INT64_0_K8.end() - 1);
    REQUIRE_THROWS_AS(deserialize_bytes(bytes), std::invalid_argument);
    // a non-empty flag on an 8-byte buffer
    bytes_t preamble_only = EMPTY_K8;
    preamble_only[3] = 0;
    REQUIRE_THROWS_AS(deserialize_bytes(preamble_only), std::invalid_argument);
  }
  SECTION("slot value greater than INT64_MAX") {
    bytes_t bytes = INT64_0_K8;
    bytes[8 + 8 * 7 + 7] |= 0x80;  // top bit of the last slot
    check_rejected<std::invalid_argument>(bytes);
  }
}

TEST_CASE("minhash serde: truncated stream", "[minhash_sketch_serde]") {
  // in the preamble
  REQUIRE_THROWS_AS(deserialize_stream(bytes_t(INT64_0_K8.begin(), INT64_0_K8.begin() + 5)), std::runtime_error);
  // in the slots
  REQUIRE_THROWS_AS(deserialize_stream(bytes_t(INT64_0_K8.begin(), INT64_0_K8.end() - 1)), std::runtime_error);
  // a non-empty flag with no slots
  bytes_t preamble_only = EMPTY_K8;
  preamble_only[3] = 0;
  REQUIRE_THROWS_AS(deserialize_stream(preamble_only), std::runtime_error);
}

using minhash_sketch_test_alloc = minhash_sketch_alloc<test_allocator<uint64_t>>;

TEST_CASE("minhash serde: allocator", "[minhash_sketch_serde]") {
  test_allocator_total_bytes = 0;
  test_allocator_net_allocations = 0;
  {
    auto sketch = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(300).set_seed(123).build();
    for (uint64_t i = 0; i < 1000; ++i) { sketch.update(i); }
    const auto bytes = sketch.serialize();
    REQUIRE(test_allocator_net_allocations > 0);
    const auto d1 = minhash_sketch_test_alloc::deserialize(bytes.data(), bytes.size(), 123,
        test_allocator<uint64_t>(0));
    REQUIRE(d1.get_signature() == sketch.get_signature());

    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    sketch.serialize(s);
    const auto d2 = minhash_sketch_test_alloc::deserialize(s, 123, test_allocator<uint64_t>(0));
    REQUIRE(d2.get_signature() == sketch.get_signature());
    REQUIRE_FALSE(d2.is_empty());
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

} // namespace datasketches

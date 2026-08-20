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

#include <cstring>
#include <sstream>

#include <catch2/catch.hpp>

#include <test_allocator.hpp>

#include "minhash_sketch.hpp"

namespace datasketches {

TEST_CASE("minhash sketch serde: exact byte layout", "[minhash_sketch]") {
  // this layout is a cross-language contract: pin every field to its offset
  auto sketch = minhash_sketch::builder().set_k(112).set_seed(123).build();
  sketch.update(static_cast<uint64_t>(1));
  const auto bytes = sketch.serialize();

  REQUIRE(bytes[0] == 2);   // preamble longs
  REQUIRE(bytes[1] == 1);   // serial version
  REQUIRE(bytes[2] == 23);  // family id (drafted, pending dev@ assignment)
  REQUIRE(bytes[3] == 0);   // flags: not empty
  uint16_t k;
  std::memcpy(&k, bytes.data() + 4, sizeof(k));
  REQUIRE(k == 112);
  uint16_t seed_hash;
  std::memcpy(&seed_hash, bytes.data() + 6, sizeof(seed_hash));
  REQUIRE(seed_hash == compute_seed_hash(123));
  for (size_t i = 8; i < 16; ++i) REQUIRE(bytes[i] == 0);
  uint64_t first_word;
  std::memcpy(&first_word, bytes.data() + 16, sizeof(first_word));
  REQUIRE(first_word == sketch.get_signature()[0]);

  // empty sketch: flags bit 0 set, no payload
  const auto empty_bytes = minhash_sketch::builder().set_k(112).build().serialize();
  REQUIRE(empty_bytes.size() == 16);
  REQUIRE(empty_bytes[3] == 1);

  // stream and byte serialization produce identical bytes
  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  sketch.serialize(s);
  const std::string stream_bytes = s.str();
  REQUIRE(stream_bytes.size() == bytes.size());
  REQUIRE(std::memcmp(stream_bytes.data(), bytes.data(), bytes.size()) == 0);
}

TEST_CASE("minhash sketch serde: empty round trip", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().set_k(112).build();
  REQUIRE(sketch.get_serialized_size_bytes() == 16);

  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  sketch.serialize(s);
  REQUIRE(s.str().size() == sketch.get_serialized_size_bytes());
  auto from_stream = minhash_sketch::deserialize(s);
  REQUIRE(from_stream.is_empty());
  REQUIRE(from_stream.get_num_hashes() == 112);
  REQUIRE(from_stream.get_seed() == DEFAULT_SEED);
  REQUIRE(from_stream.get_signature() == sketch.get_signature());

  const auto bytes = sketch.serialize();
  REQUIRE(bytes.size() == sketch.get_serialized_size_bytes());
  auto from_bytes = minhash_sketch::deserialize(bytes.data(), bytes.size());
  REQUIRE(from_bytes.is_empty());
  REQUIRE(from_bytes.get_num_hashes() == 112);
}

TEST_CASE("minhash sketch serde: non-empty round trip", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().set_k(112).set_seed(123).build();
  for (uint64_t i = 0; i < 1000; ++i) sketch.update(i);
  REQUIRE(sketch.get_serialized_size_bytes() == 16 + 8 * 112);

  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  sketch.serialize(s);
  REQUIRE(s.str().size() == sketch.get_serialized_size_bytes());
  auto from_stream = minhash_sketch::deserialize(s, 123);
  REQUIRE(!from_stream.is_empty());
  REQUIRE(from_stream.get_num_hashes() == 112);
  REQUIRE(from_stream.get_seed() == 123);
  REQUIRE(from_stream.get_signature() == sketch.get_signature());
  REQUIRE(from_stream.jaccard(sketch) == 1.0);

  const auto bytes = sketch.serialize();
  REQUIRE(bytes.size() == sketch.get_serialized_size_bytes());
  auto from_bytes = minhash_sketch::deserialize(bytes.data(), bytes.size(), 123);
  REQUIRE(!from_bytes.is_empty());
  REQUIRE(from_bytes.get_signature() == sketch.get_signature());
  REQUIRE(from_bytes.jaccard(sketch) == 1.0);
  REQUIRE(from_bytes.get_serialized_size_bytes() == sketch.get_serialized_size_bytes());
}

TEST_CASE("minhash sketch serde: nonzero header size", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().build();
  for (uint64_t i = 0; i < 100; ++i) sketch.update(i);

  const unsigned header = 7;
  const auto bytes = sketch.serialize(header);
  REQUIRE(bytes.size() == header + sketch.get_serialized_size_bytes());
  auto deserialized = minhash_sketch::deserialize(bytes.data() + header, bytes.size() - header);
  REQUIRE(deserialized.get_signature() == sketch.get_signature());
}

TEST_CASE("minhash sketch serde: deserialize failures", "[minhash_sketch]") {
  auto sketch = minhash_sketch::builder().set_k(112).build();
  for (uint64_t i = 0; i < 1000; ++i) sketch.update(i);
  const auto bytes = sketch.serialize();

  // truncated buffers
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(bytes.data(), 8), std::out_of_range);
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(bytes.data(), bytes.size() - 1), std::out_of_range);

  // corrupted preamble longs
  auto corrupt = bytes;
  corrupt[0] = 3;
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(corrupt.data(), corrupt.size()), std::invalid_argument);

  // corrupted serial version
  corrupt = bytes;
  corrupt[1] = 99;
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(corrupt.data(), corrupt.size()), std::invalid_argument);

  // corrupted family id
  corrupt = bytes;
  corrupt[2] = 18;
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(corrupt.data(), corrupt.size()), std::invalid_argument);

  // corrupted k == 0
  corrupt = bytes;
  corrupt[4] = 0;
  corrupt[5] = 0;
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(corrupt.data(), corrupt.size()), std::invalid_argument);

  // wrong seed
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(bytes.data(), bytes.size(), 999), std::invalid_argument);

  // stream variants: header corruption, wrong seed and truncation
  corrupt = bytes;
  corrupt[0] = 3;
  std::stringstream s0(std::ios::in | std::ios::out | std::ios::binary);
  s0.write(reinterpret_cast<const char*>(corrupt.data()), corrupt.size());
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(s0), std::invalid_argument);
  std::stringstream s1(std::ios::in | std::ios::out | std::ios::binary);
  sketch.serialize(s1);
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(s1, 999), std::invalid_argument);
  std::stringstream s2(std::ios::in | std::ios::out | std::ios::binary);
  s2.write(reinterpret_cast<const char*>(bytes.data()), 10);
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(s2), std::runtime_error);
  std::stringstream s3(std::ios::in | std::ios::out | std::ios::binary);
  s3.write(reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1);
  REQUIRE_THROWS_AS(minhash_sketch::deserialize(s3), std::runtime_error);
}

TEST_CASE("minhash sketch serde: allocator", "[minhash_sketch]") {
  using minhash_sketch_test_alloc = minhash_sketch_alloc<test_allocator<uint64_t>>;
  test_allocator_total_bytes = 0;
  test_allocator_net_allocations = 0;
  {
    auto sketch = minhash_sketch_test_alloc::builder(test_allocator<uint64_t>(0)).set_k(300).build();
    for (uint64_t i = 0; i < 100; ++i) sketch.update(i);
    const auto bytes = sketch.serialize();
    auto deserialized = minhash_sketch_test_alloc::deserialize(bytes.data(), bytes.size(),
        DEFAULT_SEED, test_allocator<uint64_t>(0));
    REQUIRE(deserialized.get_signature() == sketch.get_signature());
  }
  REQUIRE(test_allocator_total_bytes == 0);
  REQUIRE(test_allocator_net_allocations == 0);
}

} /* namespace datasketches */

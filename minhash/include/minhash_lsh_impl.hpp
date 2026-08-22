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

#ifndef MINHASH_LSH_IMPL_HPP_
#define MINHASH_LSH_IMPL_HPP_

#include <algorithm>
#include <stdexcept>

#include "MurmurHash3.h"

namespace datasketches {

template<typename A>
minhash_lsh_index_alloc<A>::minhash_lsh_index_alloc(uint16_t num_bands, double jaccard_threshold,
    const A& allocator):
num_bands_(num_bands),
threshold_(jaccard_threshold),
allocator_(allocator),
bands_(num_bands, bucket_map(0, std::hash<uint64_t>(), std::equal_to<uint64_t>(),
    typename bucket_map::allocator_type(allocator)),
    typename std::vector<bucket_map, rebind_alloc<bucket_map>>::allocator_type(allocator)),
sketches_(0, std::hash<uint64_t>(), std::equal_to<uint64_t>(),
    typename sketch_map::allocator_type(allocator))
{
  if (num_bands == 0) throw std::invalid_argument("num_bands must be at least 1");
  // the negated comparison also rejects NaN
  if (!(jaccard_threshold > 0.0) || jaccard_threshold > 1.0) {
    throw std::invalid_argument("jaccard_threshold must be in (0, 1]");
  }
}

template<typename A>
auto minhash_lsh_index_alloc<A>::band_keys(const sketch_type& sketch, uint16_t num_bands) -> vector_u64 {
  if (num_bands == 0) throw std::invalid_argument("num_bands must be at least 1");
  const uint32_t k = sketch.get_num_hashes();
  if (num_bands > k) {
    throw std::invalid_argument("num_bands " + std::to_string(num_bands)
        + " must not exceed k " + std::to_string(k));
  }
  if (k % num_bands != 0) {
    throw std::invalid_argument("num_bands " + std::to_string(num_bands)
        + " must divide k " + std::to_string(k) + " evenly");
  }
  if (sketch.is_empty()) throw std::invalid_argument("cannot compute band keys of an empty sketch");
  const uint32_t rows = k / num_bands;
  vector_u64 keys(sketch.get_signature().get_allocator());
  keys.reserve(num_bands);
  for (uint16_t band = 0; band < num_bands; ++band) {
    HashState hashes;
    MurmurHash3_x64_128(sketch.get_signature().data() + static_cast<size_t>(band) * rows,
        rows * sizeof(uint64_t), sketch.get_seed() ^ band, hashes);
    keys.push_back(hashes.h1);
  }
  return keys;
}

template<typename A>
void minhash_lsh_index_alloc<A>::add(uint64_t id, const sketch_type& sketch) {
  check_compatible(sketch);
  if (sketches_.find(id) != sketches_.end()) {
    throw std::invalid_argument("id already in the index: " + std::to_string(id));
  }
  const auto keys = band_keys(sketch, num_bands_); // validates divisibility and non-empty
  // store the sketch before the band buckets: if a bucket insertion then fails, the
  // document is merely under-indexed, whereas an id in a bucket with no stored sketch
  // would make every later get_duplicate_clusters() throw
  sketches_.emplace(id, sketch);
  for (uint16_t band = 0; band < num_bands_; ++band) {
    auto result = bands_[band].emplace(keys[band], vector_u64(allocator_));
    result.first->second.push_back(id);
  }
}

template<typename A>
auto minhash_lsh_index_alloc<A>::query_candidates(const sketch_type& sketch) const -> vector_u64 {
  check_compatible(sketch);
  const auto keys = band_keys(sketch, num_bands_);
  vector_u64 result(allocator_);
  for (uint16_t band = 0; band < num_bands_; ++band) {
    const auto it = bands_[band].find(keys[band]);
    if (it != bands_[band].end()) {
      result.insert(result.end(), it->second.begin(), it->second.end());
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

template<typename A>
auto minhash_lsh_index_alloc<A>::get_duplicate_clusters() const -> vector_of_clusters {
  // sorted ids give deterministic cluster contents and ordering
  vector_u64 ids(allocator_);
  ids.reserve(sketches_.size());
  for (const auto& entry: sketches_) ids.push_back(entry.first);
  std::sort(ids.begin(), ids.end());

  using index_map = std::unordered_map<uint64_t, size_t, std::hash<uint64_t>,
      std::equal_to<uint64_t>, rebind_alloc<std::pair<const uint64_t, size_t>>>;
  index_map index_of(0, std::hash<uint64_t>(), std::equal_to<uint64_t>(),
      typename index_map::allocator_type(allocator_));
  for (size_t i = 0; i < ids.size(); ++i) index_of.emplace(ids[i], i);

  // union-find with path halving and union by size
  std::vector<size_t, rebind_alloc<size_t>> parent(ids.size(), 0, rebind_alloc<size_t>(allocator_));
  std::vector<size_t, rebind_alloc<size_t>> set_size(ids.size(), 1, rebind_alloc<size_t>(allocator_));
  for (size_t i = 0; i < parent.size(); ++i) parent[i] = i;
  auto find = [&parent](size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };

  for (const auto& band: bands_) {
    for (const auto& bucket: band) {
      const auto& members = bucket.second;
      // pairs within a bucket are examined pairwise: quadratic in the bucket size,
      // which is fine for near-duplicate buckets but can bite on heavily skewed data
      for (size_t i = 0; i + 1 < members.size(); ++i) {
        for (size_t j = i + 1; j < members.size(); ++j) {
          size_t root_a = find(index_of.at(members[i]));
          size_t root_b = find(index_of.at(members[j]));
          if (root_a == root_b) continue; // already linked, skip re-verification
          if (sketches_.at(members[i]).jaccard(sketches_.at(members[j])) >= threshold_) {
            if (set_size[root_a] < set_size[root_b]) std::swap(root_a, root_b);
            parent[root_b] = root_a;
            set_size[root_a] += set_size[root_b];
          }
        }
      }
    }
  }

  const rebind_alloc<vector_u64> cluster_alloc(allocator_);
  vector_of_clusters clusters(cluster_alloc);
  using root_map = std::unordered_map<size_t, size_t, std::hash<size_t>,
      std::equal_to<size_t>, rebind_alloc<std::pair<const size_t, size_t>>>;
  root_map cluster_of_root(0, std::hash<size_t>(), std::equal_to<size_t>(),
      typename root_map::allocator_type(allocator_));
  for (size_t i = 0; i < ids.size(); ++i) {
    const size_t root = find(i);
    if (set_size[root] < 2) continue; // no verified duplicates
    auto result = cluster_of_root.emplace(root, clusters.size());
    if (result.second) clusters.push_back(vector_u64(allocator_));
    clusters[result.first->second].push_back(ids[i]);
  }
  return clusters;
}

template<typename A>
size_t minhash_lsh_index_alloc<A>::get_num_documents() const {
  return sketches_.size();
}

template<typename A>
void minhash_lsh_index_alloc<A>::check_compatible(const sketch_type& sketch) const {
  if (sketches_.empty()) return;
  const auto& first = sketches_.begin()->second;
  if (sketch.get_num_hashes() != first.get_num_hashes() || sketch.get_seed() != first.get_seed()) {
    throw std::invalid_argument("sketch is incompatible with the index: k and seed must match earlier adds");
  }
}

} /* namespace datasketches */

#endif

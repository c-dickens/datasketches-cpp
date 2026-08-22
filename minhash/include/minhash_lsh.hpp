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

#ifndef MINHASH_LSH_HPP_
#define MINHASH_LSH_HPP_

#include <unordered_map>
#include <vector>

#include "minhash_sketch.hpp"

namespace datasketches {

// forward declaration
template<typename A> class minhash_lsh_index_alloc;

/// MinHash LSH index alias with default allocator
using minhash_lsh_index = minhash_lsh_index_alloc<std::allocator<uint64_t>>;

/**
 * Locality-sensitive-hashing index over MinHash signatures (the classic banding scheme).
 *
 * The k signature slots are split into num_bands contiguous bands of r = k / num_bands
 * slots each (num_bands must divide k). Each band is hashed to a single 64-bit key.
 * Two documents become candidate duplicates if any band produces the same key, which
 * happens with probability 1 - (1 - J^r)^b for true Jaccard similarity J.
 * Candidate pairs are verified against the estimated Jaccard threshold before being
 * reported, so chance band collisions between dissimilar documents are filtered out.
 *
 * This index is a single-machine convenience. For distributed pipelines, band_keys()
 * is the primitive: emit (band index, band key) pairs and group by key.
 */
template<typename Allocator = std::allocator<uint64_t>>
class minhash_lsh_index_alloc {
public:
  using allocator_type = Allocator;
  using sketch_type = minhash_sketch_alloc<Allocator>;
  using vector_u64 = std::vector<uint64_t, Allocator>;
  template<typename T> using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  using vector_of_clusters = std::vector<vector_u64, rebind_alloc<vector_u64>>;

  /**
   * Creates an empty index.
   * @param num_bands number of bands (must be at least 1; must divide the k of added sketches)
   * @param jaccard_threshold verification threshold in (0, 1]: candidate pairs with an
   * estimated Jaccard similarity below this value are not reported as duplicates
   * @param allocator instance of an Allocator
   * @throws std::invalid_argument if num_bands is 0 or jaccard_threshold is outside (0, 1]
   */
  minhash_lsh_index_alloc(uint16_t num_bands, double jaccard_threshold,
      const Allocator& allocator = Allocator());

  /**
   * Computes one 64-bit key per band of the given sketch.
   * Band j covers signature slots [j * r, (j + 1) * r) with r = k / num_bands, hashed
   * in native byte order with seed = sketch seed ^ j (band keys are an in-memory
   * primitive, not a serialization format).
   * @param sketch the sketch to compute band keys of
   * @param num_bands number of bands
   * @return one key per band, in band order
   * @throws std::invalid_argument if num_bands is 0, exceeds k or does not divide k
   * evenly, or if the sketch is empty
   */
  static vector_u64 band_keys(const sketch_type& sketch, uint16_t num_bands);

  /**
   * Adds a document's sketch to the index under the given id.
   * The first add fixes the expected k and seed; later adds must match.
   * @param id unique document identifier
   * @param sketch the document's MinHash sketch (a copy is stored for verification)
   * @throws std::invalid_argument if the id is already present, the sketch is empty,
   * k is not divisible by num_bands, or k or seed do not match earlier adds
   */
  void add(uint64_t id, const sketch_type& sketch);

  /**
   * Returns the ids of indexed documents that share at least one band key with the
   * given sketch (candidate duplicates, not yet verified against the threshold).
   * @param sketch the query sketch
   * @return sorted, deduplicated ids (empty if nothing matches)
   * @throws std::invalid_argument if the sketch is empty, k is not divisible by
   * num_bands, or k or seed do not match the indexed sketches
   */
  vector_u64 query_candidates(const sketch_type& sketch) const;

  /**
   * Groups indexed documents into duplicate clusters: candidate pairs whose estimated
   * Jaccard similarity is at least the threshold are linked, and clusters are the
   * connected components of those links (transitive closures, not cliques — two
   * members of the same cluster may be linked only through intermediates).
   * @return clusters of at least 2 ids; each cluster sorted ascending, clusters
   * ordered by their smallest id
   */
  vector_of_clusters get_duplicate_clusters() const;

  /// @return the number of documents in the index
  size_t get_num_documents() const;

private:
  using bucket_map = std::unordered_map<uint64_t, vector_u64, std::hash<uint64_t>,
      std::equal_to<uint64_t>, rebind_alloc<std::pair<const uint64_t, vector_u64>>>;
  using sketch_map = std::unordered_map<uint64_t, sketch_type, std::hash<uint64_t>,
      std::equal_to<uint64_t>, rebind_alloc<std::pair<const uint64_t, sketch_type>>>;

  uint16_t num_bands_;
  double threshold_;
  Allocator allocator_;
  std::vector<bucket_map, rebind_alloc<bucket_map>> bands_; // per band: key -> ids
  sketch_map sketches_; // id -> sketch, for verification

  void check_compatible(const sketch_type& sketch) const;
};

} /* namespace datasketches */

#include "minhash_lsh_impl.hpp"

#endif

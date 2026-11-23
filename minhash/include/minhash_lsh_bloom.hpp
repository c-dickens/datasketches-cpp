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

#ifndef MINHASH_LSH_BLOOM_HPP_
#define MINHASH_LSH_BLOOM_HPP_

#include <memory>
#include <vector>

#include "bloom_filter.hpp"
#include "minhash_lsh.hpp"
#include "minhash_sketch.hpp"

namespace datasketches {

// forward declaration
template<typename A> class minhash_lsh_bloom_alloc;

/// LSHBloom alias with default allocator
using minhash_lsh_bloom = minhash_lsh_bloom_alloc<std::allocator<uint64_t>>;

/**
 * LSHBloom: one Bloom filter per band, for one-pass removal of near-copies
 * (arXiv 2411.04257).
 *
 * The k slots of a signature are split into b bands, as in minhash_lsh_index, and each
 * band gives one band key (minhash/SPEC.md, "Band keys"). Band key j goes into Bloom
 * filter j. A document is flagged when at least one of its band keys is already in the
 * filter of its band, that is when it shares a band key with an earlier document (or
 * when a Bloom filter gives a false positive). The class keeps no sketches and no ids,
 * so it uses much less memory than minhash_lsh_index.
 *
 * Read these rules before you use it:
 * - The first copy wins. The order of the input decides which copy of a document is not
 *   flagged, and with chains of copies it can also decide how many are not flagged.
 *   Sort the input first if that choice matters.
 * - Flagged documents are inserted too. Each document is checked against all earlier
 *   documents, so chains are found: if B is like A and C is like B, C is flagged.
 * - There is no Jaccard check. The flag rate for two documents of Jaccard similarity J
 *   is the LSH curve 1 - (1 - J^r)^b with r = k / b, so some documents a little below
 *   the intended threshold are flagged too. Choose b and r with the guidance table in
 *   scripts/minhash/results/characterisation/lsh_accuracy.md.
 * - There is no capacity check. If more than max_distinct_items documents go in, the
 *   false positive rate goes above the target, as for bloom_filter.
 * - Use minhash_lsh_index when you need the groups of copies or a Jaccard check.
 */
template<typename Allocator = std::allocator<uint64_t>>
class minhash_lsh_bloom_alloc {
public:
  using allocator_type = Allocator;
  using sketch_type = minhash_sketch_alloc<Allocator>;
  template<typename T> using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  using filter_type = bloom_filter_alloc<rebind_alloc<uint8_t>>;

  /**
   * Creates b empty Bloom filters, each sized by accuracy for max_distinct_items items
   * and the per-band rate p = 1 - (1 - p_eff)^(1/b).
   * @param num_bands number of bands b (at least 1; must divide the k of the sketches)
   * @param max_distinct_items expected number of documents (at least 1)
   * @param target_false_positive_prob p_eff in (0, 1): the probability that a document
   *   with no band key in the filters is still flagged, when the filters hold
   *   max_distinct_items documents
   * @param seed Bloom filter seed, the same for every band
   * @param allocator instance of an Allocator
   * @throws std::invalid_argument for any value out of range (NaN included)
   */
  minhash_lsh_bloom_alloc(uint16_t num_bands, uint64_t max_distinct_items,
      double target_false_positive_prob, uint64_t seed = DEFAULT_SEED,
      const Allocator& allocator = Allocator());

  /**
   * Inserts the b band keys of the sketch. The first update or query_and_update fixes
   * k and the sketch seed.
   * @param sketch a non-empty sketch
   * @throws std::invalid_argument if the sketch is empty, b does not divide its k, or
   *   k or the seed differ from the first sketch
   */
  void update(const sketch_type& sketch);

  /**
   * Checks the sketch without changing anything.
   * @param sketch a non-empty sketch
   * @return true if the document is flagged: one of its band keys is in its band's filter
   * @throws std::invalid_argument as update()
   */
  bool query(const sketch_type& sketch) const;

  /**
   * Checks the sketch, then inserts all of its b band keys, flagged or not.
   * @param sketch a non-empty sketch
   * @return true if the document is flagged: one of its band keys was already in its
   *   band's filter
   * @throws std::invalid_argument as update()
   */
  bool query_and_update(const sketch_type& sketch);

  /// @return the total number of bits in the b filters
  uint64_t get_capacity() const;

private:
  uint16_t num_bands_;
  bool is_fixed_ = false; // true once a sketch has fixed k and seed
  uint32_t k_ = 0;
  uint64_t sketch_seed_ = 0;
  std::vector<filter_type, rebind_alloc<filter_type>> filters_;

  void check_compatible(const sketch_type& sketch) const;
  void fix(const sketch_type& sketch);
};

} /* namespace datasketches */

#include "minhash_lsh_bloom_impl.hpp"

#endif

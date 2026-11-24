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

#ifndef DEMO_DEMO_HPP_
#define DEMO_DEMO_HPP_

// Fork-only demo code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Shared helpers of the MinHash and LSH demos: the pairs to check, and the
// per-bin statistics that compare Jaccard estimates with exact values.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "minhash_sketch.hpp"

namespace demo {

constexpr std::array<uint16_t, 4> K_VALUES = {64, 128, 256, 512};
constexpr std::size_t NUM_BINS = 10;          // J bins of width 0.1
constexpr std::size_t MIN_BIN_PAIRS = 1000;   // a bin with fewer pairs is reported, not checked
constexpr double MAX_MEAN_BIAS = 0.01;
constexpr double SPREAD_TOLERANCE = 0.15;
constexpr double NOMINAL_COVERAGE_2SD = 0.9545;
constexpr double NOMINAL_COVERAGE_3SD = 0.9973;
constexpr double COVERAGE_SLACK = 0.01;       // "minus 1 point"
constexpr uint64_t SAMPLE_SEED = 1;
constexpr std::size_t SAMPLE_SIZE = 2000;
constexpr std::size_t MERGE_PIECES = 4;

/// The bin of an exact J: floor(10 J). J = 1 is in bin 9.
std::size_t jaccard_bin(double j);

/// The `count` indices i in [0, n) with the smallest XXHash64::hash(&i, 8, seed),
/// with i as uint64_t and ties by i. The result is in ascending order.
/// Throws std::invalid_argument if count > n.
std::vector<std::size_t> sample_indices(std::size_t n, std::size_t count, uint64_t seed);

/// The pairs to check: all pairs of sample_indices(keys.size(), sample_size, seed), plus all
/// pairs i < j with keys[i] == keys[j] and keys[i] not empty. Each pair is (smaller, larger).
/// The result is sorted and has no duplicates.
std::vector<std::pair<std::size_t, std::size_t>> check_pairs(
    const std::vector<std::string>& keys, std::size_t sample_size, uint64_t seed);

/// A sketch with k slots and the default seed (9001) that received every item.
datasketches::minhash_sketch sketch_of(const std::vector<std::string>& items, uint16_t k);

struct pair_estimate {
  double exact;                   // exact J
  double estimate;                // a.jaccard(b)
  std::array<double, 3> bounds2;  // a.jaccard_bounds(b, 2)
  std::array<double, 3> bounds3;  // a.jaccard_bounds(b, 3)
};

struct bin_summary {
  std::size_t bin;
  std::size_t pairs;
  double mean_bias;
  double observed_sd;
  double expected_sd;
  double coverage2;
  double coverage3;
  bool checked;  // pairs >= MIN_BIN_PAIRS
  bool bias_pass;
  bool spread_pass;
  bool coverage2_pass;
  bool coverage3_pass;
};

/// Puts the pairs of one k into NUM_BINS bins. Pairs with exact J = 0 or J = 1 are left out.
/// The result has NUM_BINS entries, in bin order, also for a bin with no pair.
std::vector<bin_summary> summarise_bins(const std::vector<pair_estimate>& pairs, uint16_t k);

/// True if the bin is not checked, or if it passes every criterion.
bool bin_passes(const bin_summary& bin);

/// A Markdown table with one row for each bin: the pair count, each figure, and PASS, FAIL
/// or "not checked" for each criterion.
std::string bins_table(const std::vector<bin_summary>& bins);

// LSH demo (MH-14). MH-15 uses these names too.

constexpr uint16_t LSH_K = 128;
constexpr uint16_t LSH_BANDS = 32;
constexpr unsigned LSH_ROWS = 4;           // LSH_K / LSH_BANDS
constexpr double LSH_THRESHOLD = 0.5;
constexpr std::size_t LARGEST_BUCKETS = 10;
constexpr std::size_t BUCKET_EXAMPLES = 3;

/// 1 - (1 - j^rows)^bands.
double candidate_probability(double j, unsigned rows, unsigned bands);

/// True when the two key lists are equal in at least one position (band).
/// Throws std::invalid_argument if the sizes differ.
bool is_candidate(const std::vector<uint64_t>& keys_a, const std::vector<uint64_t>& keys_b);

constexpr double SCURVE_ALPHA = 1e-6;              // false-FAIL rate of the whole table (Bonferroni over the bins)
constexpr std::size_t SCURVE_PAIRS_PER_BIN = 2000;
constexpr uint64_t SCURVE_UNION = 1000;            // |S1 ∪ S2| of each synthetic pair

/// The two-sided exact p-value of `observed` for a sum of independent Bernoulli(p_i):
/// min(1, 2 min(P(X <= observed), P(X >= observed))).
double poisson_binomial_p_value(const std::vector<double>& probs, std::size_t observed);

struct scurve_bin {
  std::size_t bin;
  std::size_t pairs;
  std::size_t candidates;
  double expected;  // sum of p_i, with p_i = candidate_probability(J_i, rows, bands)
  double p_value;   // poisson_binomial_p_value of the candidate count
  bool filled;      // pairs >= MIN_BIN_PAIRS
  bool pass;        // filled and p_value >= SCURVE_ALPHA / NUM_BINS
};

/// Collects the S-curve check one pair at a time. It keeps each p_i, for the exact p-value.
class scurve_counter {
public:
  scurve_counter(unsigned rows, unsigned bands);
  void add(double exact, bool candidate);
  std::vector<scurve_bin> bins() const;  // NUM_BINS bins, by jaccard_bin(exact)

private:
  unsigned rows_;
  unsigned bands_;
  std::array<std::vector<double>, NUM_BINS> probs_{};
  std::array<std::size_t, NUM_BINS> candidates_{};
};

/// True when every bin passes.
bool scurve_passes(const std::vector<scurve_bin>& bins);

/// The band keys of one sketch. The real function is minhash_lsh_index::band_keys(sketch, LSH_BANDS).
using band_key_function = std::function<std::vector<uint64_t>(const datasketches::minhash_sketch&)>;

/// The S-curve check on synthetic integer sets. Each bin gets SCURVE_PAIRS_PER_BIN pairs
/// S1 = [o, o + a + c) and S2 = [o + a, o + 2a + c), with 2a + c = SCURVE_UNION, so J = c / SCURVE_UNION.
/// c takes the even values of the bin in turn. Pair i has o = i * 2^32, so no item is in 2 pairs.
/// The sketches have k = rows * bands and the default seed. A pair is a candidate when
/// is_candidate(keys_of(s1), keys_of(s2)).
std::vector<scurve_bin> scurve_check(const band_key_function& keys_of, unsigned rows, unsigned bands);

/// A Markdown table with one row for each bin, and PASS, FAIL or "FAIL (bin not filled)".
std::string scurve_table(const std::vector<scurve_bin>& bins);

/// Counts for recall and precision. recall = both / similar, precision = both / verified.
struct pair_counts {
  std::size_t similar = 0;   // exact J >= LSH_THRESHOLD
  std::size_t verified = 0;  // candidate and estimated J >= LSH_THRESHOLD
  std::size_t both = 0;
  void add(bool is_similar, bool is_verified);
};

/// One bucket: the items that share one band key in one band.
struct bucket {
  uint16_t band;
  uint64_t key;
  std::vector<std::size_t> items;  // ascending
};

/// The `count` largest buckets: by size (descending), then band, then key.
/// keys_per_item[i] is band_keys of item i. An empty list means that item i is not indexed.
std::vector<bucket> largest_buckets(const std::vector<std::vector<uint64_t>>& keys_per_item, std::size_t count);

// Full-data check (MH-14b): the sketch and the LSH clusters against the exact Jaccard of every pair.
// MH-15 uses these names too.

constexpr std::array<uint16_t, 5> FULL_K_VALUES = {64, 128, 256, 512, 1024};
constexpr std::array<unsigned, 4> FULL_ROWS = {2, 4, 8, 16};
constexpr std::array<double, 7> FULL_THRESHOLDS = {0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
constexpr double FULL_ALPHA = 1e-6;           // false-FAIL rate of all the pair tests (Bonferroni)
constexpr double FULL_TARGET = 0.95;          // the recall and precision of a recommended k
constexpr double ESTIMATE_MIN_J = 0.1;        // the estimate error uses the pairs with exact J >= this
constexpr std::size_t MAX_EXACT_PAIRS = 50000000;
constexpr std::size_t WORST_MERGES = 5;
constexpr std::size_t SAMPLE_CLUSTERS = 12;          // LSH clusters drawn at random for the report
constexpr std::size_t SAMPLE_CLUSTER_MIN_SIZE = 3;   // only clusters with at least this many items
constexpr uint64_t HELDOUT_SEED = 2;                 // the index set / query set split

struct grid_point {
  uint16_t k;
  unsigned rows;
  unsigned bands;  // k / rows
  double threshold;
};

/// Every (k, r, t) of FULL_K_VALUES x FULL_ROWS x FULL_THRESHOLDS, in that order (140 points).
std::vector<grid_point> full_grid();

/// The smallest m with m / k >= t, compared in double as minhash_sketch::jaccard() is.
/// Ĵ >= t exactly when the signatures are equal in at least m slots. t is in (0, 1].
std::size_t min_matches(double t, uint16_t k);

/// pmf[s] = P(Binomial(k, j) = s), for s in [0, k].
std::vector<double> binomial_pmf(double j, std::size_t k);

/// q[s] = P(some band has all its `rows` slots equal | exactly s of the k = rows * bands slots are
/// equal), for s in [0, k]. Given s, the equal slots are a uniform s-subset, because the slots are
/// independent and have the same J. A dynamic program over the bands, with a hypergeometric count
/// in each band.
std::vector<double> full_band_given_matches(unsigned rows, unsigned bands);

/// tail[m] = sum over s >= m of pmf[s] q[s], for m in [0, k + 1]: P(some band is all-equal AND at
/// least m slots are equal). This is the joint probability that a pair is reported, not the product
/// of P(candidate) and P(Ĵ >= t), which use the same slots.
std::vector<double> reported_tail(const std::vector<double>& pmf, const std::vector<double>& q);

/// P(reported | J): reported_tail(binomial_pmf(j, k), full_band_given_matches(rows, bands))[m].
double reported_probability(double j, unsigned rows, unsigned bands, std::size_t min_matches);

/// The two pair tests of one grid point. The tested pairs are the true pairs (exact J >= t) for
/// recall, and the pairs with 0 < J < t for precision. A pair with J = 0 has probability 0.
/// The p-values treat the pairs as independent. Pairs that share an item use the same signature,
/// so they are positively correlated, and the p-values are smaller than the true ones.
struct pair_check {
  pair_counts counts;        // similar: J >= t; verified: reported
  std::size_t false_pairs;   // pairs with 0 < J < t
  double expected_true;      // sum of P(reported | J) over the true pairs
  double expected_false;     // sum of P(reported | J) over the pairs with 0 < J < t
  double p_recall;           // poisson_binomial_p_value of counts.both
  double p_precision;        // poisson_binomial_p_value of counts.verified - counts.both
  bool pass;                 // both p-values >= test_alpha
};

/// exact[i], prob[i] and reported[i] describe pair i, for every pair with J > 0. reported_disjoint
/// is the number of reported pairs with J = 0. Each p-value passes when it is >= test_alpha.
pair_check check_pair_tests(const std::vector<double>& exact, const std::vector<double>& prob,
                            const std::vector<bool>& reported, std::size_t reported_disjoint, double t,
                            double test_alpha);

using edge_list = std::vector<std::pair<std::size_t, std::size_t>>;

/// The connected components of the graph on the nodes [0, n): label[i] is the smallest node of the
/// component of i. Throws std::invalid_argument if an edge has a node >= n.
std::vector<std::size_t> components(std::size_t n, const edge_list& edges);

/// The clusters (components of 2 or more nodes), each sorted, ordered by their smallest node.
std::vector<std::vector<std::size_t>> clusters_of(const std::vector<std::size_t>& label);

/// LSH clusters against true clusters. Both labels come from components() on the same nodes.
/// A node alone in its component counts as a component of its own, so a merge can join a true
/// cluster and a node that is in no true cluster.
struct cluster_metrics {
  std::size_t lsh_clusters = 0;   // LSH components with 2 or more nodes
  std::size_t true_clusters = 0;  // true components with 2 or more nodes
  std::size_t lsh_pairs = 0;      // node pairs in one LSH component
  std::size_t true_pairs = 0;     // node pairs in one true component
  std::size_t both_pairs = 0;     // node pairs in one LSH component and in one true component
  std::size_t merges = 0;         // LSH clusters with nodes of 2 or more true components
  std::size_t splits = 0;         // true clusters with nodes in 2 or more LSH components
  std::size_t largest_lsh = 0;    // nodes in the largest LSH component (1 if no cluster)
  std::size_t largest_true = 0;
};

/// Co-membership precision = both_pairs / lsh_pairs, recall = both_pairs / true_pairs.
/// Throws std::invalid_argument if the label sizes differ.
cluster_metrics compare_clusters(const std::vector<std::size_t>& lsh_label, const std::vector<std::size_t>& true_label);

/// One LSH cluster that joins 2 or more true components.
struct merge_example {
  std::size_t lsh_root;     // the smallest node of the LSH cluster
  std::size_t nodes;
  std::size_t true_parts;   // true components among its nodes
  std::pair<std::size_t, std::size_t> weakest;  // the edge of the cluster with the lowest exact J
  double weakest_j;
};

/// The `count` worst merges: by true_parts (descending), then nodes (descending), then lsh_root.
/// edges are the LSH edges, and edge_j[e] is the exact J of edge e.
std::vector<merge_example> worst_merges(const std::vector<std::size_t>& lsh_label,
                                        const std::vector<std::size_t>& true_label, const edge_list& edges,
                                        const std::vector<double>& edge_j, std::size_t count);

/// One stored pair of the exact pass: post indices i < j and the sizes of the shingle sets.
struct exact_pair {
  std::size_t i;
  std::size_t j;
  std::size_t intersection;
  std::size_t union_size;
};

/// A fingerprint of the input of the exact pass: XXHash64 (seed 0) of the text that has, for each
/// set in order, its id in decimal and then each item, each followed by "\n", and then one more "\n".
/// Any change to the shingling changes it. Throws std::invalid_argument if the sizes differ.
uint64_t input_fingerprint(const std::vector<std::size_t>& ids, const std::vector<std::vector<std::string>>& sets);

/// The header lines of the exact-pair cache. The first line is "# " + what. A cache with another header is not used.
std::string exact_pairs_header(const std::string& what, const std::string& archive_sha256, std::size_t shingle_words,
                               std::size_t indexed_posts, uint64_t input_xxh64);

/// Writes the header, a line "# pairs=N", then one line "i\tj\tintersection\tunion" for each pair.
void write_exact_pairs(std::ostream& out, const std::string& header, const std::vector<exact_pair>& pairs);

/// Reads a cache that write_exact_pairs wrote. Returns false, and leaves `pairs` empty, if the
/// header is not `header`. Throws std::runtime_error on a malformed line, or if the number of pair
/// lines is not the "# pairs=N" of the file (for example, a truncated file).
bool read_exact_pairs(std::istream& in, const std::string& header, std::vector<exact_pair>& pairs);

/// The verified pairs of one J band (report only).
struct band_rate {
  std::size_t pairs = 0;
  std::size_t verified = 0;
  double expected = 0.0;  // sum of P(verified | J) over the pairs of the band
};

/// The pairs with J > 0 in NUM_BINS bands of width 0.1, by jaccard_bin (J = 1 is in the last band).
/// exact[i], prob[i] and verified[i] describe pair i. Pairs with J = 0 are left out.
/// Throws std::invalid_argument if the sizes differ.
std::array<band_rate, NUM_BINS> band_rates(const std::vector<double>& exact, const std::vector<double>& prob,
                                           const std::vector<bool>& verified);

/// The positions of the `count` keys with the smallest XXHash64::hash(&key, 8, seed), with each key as
/// uint64_t, ties by key, in that order. All positions if there are fewer keys. For keys 0 .. n - 1,
/// the positions are those of sample_indices(n, count, seed).
std::vector<std::size_t> smallest_hashes(const std::vector<std::size_t>& keys, std::size_t count, uint64_t seed);

/// True when XXHash64::hash(&key, 8, seed), with the key as uint64_t, is even: the item goes to the
/// index set. Otherwise it goes to the query set.
bool in_index_set(std::size_t key, uint64_t seed);

/// The band-key bucket rule, computed in the demo: the items whose band keys equal the query's
/// band keys in at least one band. It is the reference for minhash_lsh_index::query_candidates().
class bucket_table {
public:
  /// keys_per_item[i] is band_keys of item ids[i]. Throws std::invalid_argument if the sizes differ.
  bucket_table(const std::vector<std::vector<uint64_t>>& keys_per_item, const std::vector<std::size_t>& ids);
  /// The ids of the candidates, sorted, with no duplicates.
  std::vector<std::size_t> candidates(const std::vector<uint64_t>& query_keys) const;

private:
  std::vector<std::unordered_map<uint64_t, std::vector<std::size_t>>> bands_;  // only looked up
};

/// Deduplication by clusters: each component keeps its smallest item and removes the others, so an
/// item is kept when label[i] == i (labels from components()). An item alone is kept.
/// wrongly_removed and missed_duplicates compare which item each side keeps. When a merge and a split
/// meet, LSH can keep another item of the same true component, so they are upper bounds of
/// lost_components and extra_kept, which count what the LSH kept set covers.
struct kept_counts {
  std::size_t items = 0;
  std::size_t kept_exact = 0;
  std::size_t kept_lsh = 0;
  std::size_t kept_both = 0;
  std::size_t wrongly_removed = 0;    // kept by the exact clusters, removed by the LSH clusters
  std::size_t missed_duplicates = 0;  // kept by the LSH clusters, removed by the exact clusters
  std::size_t lost_components = 0;    // true components with no item in the LSH kept set
  std::size_t extra_kept = 0;         // LSH-kept items beyond one for each true component they cover
};

/// Throws std::invalid_argument if the label sizes differ.
kept_counts compare_kept(const std::vector<std::size_t>& lsh_label, const std::vector<std::size_t>& true_label);

/// The input and the names of one full-data check. Node u is an indexed item (at least one shingle).
struct full_source {
  std::string ticket;           // "MH-14b"
  std::string stem;             // of the program, the outputs and the cache: "lsh_20news_full"
  std::string dataset;          // in the report title: "20 Newsgroups"
  std::string noun;             // one item: "post"
  std::string nouns;            // "posts"
  std::string sample_note;      // after the item count of a smoke run: "the MH-8 sample"
  std::string shingle_source;   // where the shingle words come from: "after the header block of the post is removed"
  std::string shingle_sets;     // in the Truth sentence: "the MH-8 shingle sets"
  std::string archive_sha256;   // of the downloaded archive, for the cache header
  std::size_t shingle_words = 5;  // the words in one shingle: news20::SHINGLE_WORDS, or 1 for trajectories
  std::size_t loaded = 0;       // the items in the load order
  std::size_t chosen = 0;       // the items used: all, or with --smoke, the sample
  std::vector<std::size_t> item_of;              // node u is item item_of[u] of the load order
  std::vector<std::vector<std::string>> sets;    // the shingle set of node u
  std::vector<std::string> names;                // the name of node u in the report
  edge_list external;           // node pairs that must be in one true and one LSH cluster at every grid point
  std::string external_title;   // the report heading: "Cross-posts"
  std::string external_text;    // the paragraph under it
  std::string external_label;   // "cross-post pairs"
  std::string external_column;  // of the clusters TSV: "CrossPostsSplit"
  std::string bands_extra;      // Markdown after the band figure, or empty
  // The grid and the settings. The defaults are the MH-14b values; MH-15b (trajectories) changes them.
  std::vector<unsigned> rows{FULL_ROWS.begin(), FULL_ROWS.end()};
  std::vector<double> thresholds{FULL_THRESHOLDS.begin(), FULL_THRESHOLDS.end()};
  uint16_t ref_k = LSH_K;                   // the reference setting of the cluster sections
  unsigned ref_rows = LSH_ROWS;
  double ref_threshold = LSH_THRESHOLD;
  std::string ref_label = "MH-14 setting";
  uint16_t compare_k = 512;                 // the bottom row of the merge figure
  unsigned compare_rows = 2;
  double floor = 0.0;                       // > 0: store only the pairs with J >= floor, count the others in bins
  bool pair_test_in_overall = true;         // false: the pair check is report only
  std::string pair_test_note;               // Markdown after the pair check text, or empty
  std::vector<std::size_t> proxy_group;     // non-empty: the proxy group of node u, for a report-only section
  std::string proxy_title;                  // "Same-instance proxy"
  std::string proxy_text;                   // the paragraph under it
};

/// Makes a full_source for `data_dir`. smoke is true for --smoke.
using full_loader = std::function<full_source(const std::filesystem::path& data_dir, bool smoke)>;

/// The items of a full-data run: all `items`, or with smoke, sample_indices(items, SAMPLE_SIZE, SAMPLE_SEED).
std::vector<std::size_t> full_chosen(std::size_t items, bool smoke);

/// The main() of a full-data program: [--smoke] <data directory> <results directory>. It runs the
/// MH-14b check on load(data directory, smoke) at every point of full_grid(), and writes <stem>.md
/// and <stem>_{pairs,clusters,estimates}.tsv. The exact-pair cache is <stem>_exact_pairs.tsv (with
/// --smoke: <stem>_exact_pairs_smoke.tsv) next to the data directory. Returns the exit code.
int full_main(int argc, char** argv, const char* data_name, const full_loader& load);

// Reuters-21578 (MH-14c).

/// One <REUTERS> element.
struct reuters_doc {
  std::string newid;  // the NEWID attribute
  std::string text;   // the decoded <TITLE> and <BODY>, joined by "\n"; a missing one is empty
};

/// Decodes &lt;, &gt;, &amp; and &#N; (decimal, N < 256) in one pass. Other text, and an entity
/// that is not one of these, is kept as it is.
std::string decode_sgml_entities(const std::string& text);

/// Every <REUTERS> element of the text of one .sgm file, in order. Throws std::runtime_error if an
/// element has no NEWID or no </REUTERS>.
std::vector<reuters_doc> parse_reuters_sgm(const std::string& text);

}  // namespace demo

#endif  // DEMO_DEMO_HPP_

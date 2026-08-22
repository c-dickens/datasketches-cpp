# MinHash Sketches and LSH Banding

How the MinHash + LSH pipeline finds near-duplicate documents without
comparing every pair. All examples use the 6-document corpus from the demo
(`minhash_document_similarity.cpp`).

---

## 1. The pipeline (overview)

The system is a four-stage dimensionality reduction pipeline. Each stage
throws away information in a controlled way while preserving what matters:
set similarity.

```
Document (variable-length text)
  -> shingles      (sparse: ~|text| character 5-grams)
    -> MinHash signature  (dense: k=120 uint64 values)
      -> band keys        (denser: b=24 uint64 values)
        -> hash table lookups  (O(b) per query, independent of n)
```

| Stage | Input | Output | What is preserved |
|-------|-------|--------|-------------------|
| Shingling | raw text | set of 5-grams | lexical content (word-level overlap) |
| MinHash | set of any size | k=120 hash values | Jaccard similarity between sets |
| Banding | 120 values | 24 band keys | high-similarity pairs collide with high probability |
| Index lookup | 24 keys | candidate doc ids | only plausible duplicates are returned |

The final lookup cost is O(b) hash table probes -- constant with respect to
the number of documents in the index.

---

## 2. Shingling: text to sparse set

A *shingle* is a short substring. The demo uses character 5-grams: every
contiguous 5-character window in the document becomes one element of the set.

From `minhash_document_similarity.cpp:58-65`, the `sketch_document()` function:

```cpp
auto sketch = minhash_sketch::builder().set_k(k).build();
if (text.size() >= 5) {
  for (size_t i = 0; i + 5 <= text.size(); ++i)
    sketch.update(text.substr(i, 5));
}
```

For Fox-A ("The quick brown fox jumps over the lazy dog near the riverbank
every morning at dawn."), the 5-grams are: `"The q"`, `"he qu"`, `"e qui"`,
..., `"dawn."` -- 81 shingles from an 85-character string.

Fox-B differs only in the last word ("dusk" vs "dawn"), so most of its 81
shingles are identical to Fox-A's. Their shingle sets overlap heavily, and
this is what MinHash will detect.

**Important limitation**: shingling detects *lexical* near-duplicates. Fox-C
("A fast brown fox leaps across a sleepy dog by the river each morning
before sunrise.") says roughly the same thing as Fox-A, but with different
words. Its shingles are almost entirely different, so it will *not* match
Fox-A. MinHash measures surface-level text overlap, not meaning.

---

## 3. MinHash sketch: sparse set to dense signature

### How a single update works

When `update()` is called with a shingle string, the implementation at
`minhash_sketch_impl.hpp:123-135` does the following:

```cpp
void minhash_sketch_alloc<A>::update(const void* data, size_t length) {
  HashState hashes;
  MurmurHash3_x64_128(data, length, seed_, hashes);
  const uint64_t increment = hashes.h2 | 1;
  uint64_t value = hashes.h1;
  for (uint64_t& slot: signature_) {
    slot = std::min(slot, value);
    value += increment;
  }
  is_empty_ = false;
}
```

One MurmurHash3 call on the input produces two 64-bit values, h1 and h2.
Kirsch-Mitzenmacher double hashing then derives one value per slot:
`g_i = h1 + i * (h2 | 1)`. The `| 1` forces an odd increment, which
guarantees the k derived values are distinct modulo 2^64. Each slot keeps
its running minimum across all updates.

All k=120 slots are initialized to `UINT64_MAX`. After feeding every shingle
through `update()`, each slot holds the minimum of its derived hash values
across the entire shingle set.

### Why this estimates Jaccard similarity

Consider two sets A and B with true Jaccard similarity J = |A intersect B| / |A union B|.
For any single slot i, the minimum hash value comes from some element in
A union B. If that element happens to be in the intersection, both sketches
will have the same minimum in slot i. The probability of this is exactly J.

So across k=120 independent slots, the fraction of slots where both sketches
agree (`matches / k`) is an unbiased estimate of J.

### Jaccard estimation

The `jaccard()` method at `minhash_sketch_impl.hpp:146-151`:

```cpp
double minhash_sketch_alloc<A>::jaccard(const minhash_sketch_alloc& other) const {
  check_compatible(other);
  if (is_empty_ && other.is_empty_) return 1.0;
  if (is_empty_ || other.is_empty_) return 0.0;
  return static_cast<double>(count_matches(other)) / static_cast<double>(signature_.size());
}
```

The `jaccard_bounds()` method at `minhash_sketch_impl.hpp:154-166` wraps this
with Clopper-Pearson confidence intervals on the match proportion, returning
`{lower, estimate, upper}` for a given number of standard deviations.

### Merge

The `merge()` method at `minhash_sketch_impl.hpp:138-143` takes the
elementwise minimum of two signatures:

```cpp
std::transform(signature_.begin(), signature_.end(), other.signature_.begin(),
    signature_.begin(), [](uint64_t a, uint64_t b) { return std::min(a, b); });
```

The result is exactly the sketch that would have been built from the union
of the two input streams. This makes MinHash sketches mergeable across
distributed workers.

---

## 4. Band keys: dense signature to index keys

### The banding idea

The k=120 signature slots are split into b=24 contiguous bands of r=5 slots
each. Each band is hashed (MurmurHash3 of the 5 slots' raw bytes) into one
64-bit *band key*.

From `minhash_lsh_impl.hpp:50-72`, the `band_keys()` function:

```cpp
const uint32_t rows = k / num_bands;
vector_u64 keys(...);
for (uint16_t band = 0; band < num_bands; ++band) {
  HashState hashes;
  MurmurHash3_x64_128(sketch.get_signature().data() + static_cast<size_t>(band) * rows,
      rows * sizeof(uint64_t), sketch.get_seed() ^ band, hashes);
  keys.push_back(hashes.h1);
}
```

Each band uses `seed ^ band` as its hash seed, so the 24 band hashes are
independent.

### The band key grid

The demo prints a grid: 24 rows (bands) x 6 columns (documents). When two
documents have the same value in a row, they share a bucket in that band.
Conceptually (using mod-1000 shortened keys):

```
         Fox-A  Fox-B  Clim-A  Clim-B  Finance  Fox-C
B0         315    578     355      31      657    240
B1         974*   974*    607     224       56    679
B4         663*   663*    788     908      137    350
B9         750    704     156*    156*      81     83
...
```

Starred values indicate collisions: those documents share a bucket in that
band and become candidates for Jaccard verification.

### Why collisions are rare for dissimilar documents

For two documents to share a band key, *all r=5 slots in that band must be
identical*. Each slot matches with probability J (the true Jaccard
similarity), so one band matches with probability J^r. With b=24
independent bands, the probability of at least one collision is:

```
P(candidate) = 1 - (1 - J^r)^b
```

This is the classic S-curve. A few values from the demo's corpus:

| Pair | J (estimated) | J^5 (one band) | P(>=1 collision) | Meaning |
|------|---------------|-----------------|-------------------|---------|
| Fox-A vs Fox-C | ~0.17 | 0.00014 | ~0.003 | almost never collides |
| Climate-A vs Climate-B | ~0.46 | 0.021 | ~0.40 | roughly a coin flip |
| Fox-A vs Fox-B | ~0.84 | 0.42 | ~1.00 | always collides |

The S-curve has a midpoint (P=0.5) at J = (1/b)^(1/r) = (1/24)^(1/5)
which is approximately 0.55. Pairs above this similarity are very likely to
collide; pairs below it are very likely to be skipped.

---

## 5. The LSH index: keys to hash tables

### Data structure

The index (`minhash_lsh.hpp:116-125`) holds two things:

```cpp
std::vector<bucket_map, ...> bands_;   // b=24 hash tables
sketch_map sketches_;                  // id -> sketch, for verification
```

Each of the 24 entries in `bands_` is an `unordered_map<uint64_t, vector<uint64_t>>`
mapping a band key to the list of document ids that produced that key.

`sketches_` is an `unordered_map<uint64_t, sketch_type>` storing the actual
sketches so that candidate pairs can be verified with `jaccard()`.

### Adding a document

When `add(id, sketch)` is called (`minhash_lsh_impl.hpp:75-89`):

1. Compute b=24 band keys for the sketch.
2. Store the sketch in `sketches_` (for later verification).
3. For each band, push the document id into the bucket for that band's key.

```cpp
sketches_.emplace(id, sketch);
for (uint16_t band = 0; band < num_bands_; ++band) {
  auto result = bands_[band].emplace(keys[band], vector_u64(allocator_));
  result.first->second.push_back(id);
}
```

---

## 6. Use case 1: all-pairs deduplication

**Problem**: find every near-duplicate pair in a corpus of n documents.

### Brute force

Compare every pair: n(n-1)/2 Jaccard calls. With the 6-document corpus
that is 15 comparisons. At n=100,000 it would be ~5 billion.

### LSH approach

Walk every bucket in every band. Buckets with 1 member are skipped (just a
size check). Buckets with >=2 members are candidate pairs; verify them with
`jaccard()`. This is implemented in `get_duplicate_clusters()` at
`minhash_lsh_impl.hpp:108-167`.

From the demo output: LSH checked only 2 candidate pairs (Fox-A/Fox-B from
multiple band collisions, Climate-A/Climate-B from a single band
collision). The other 13 pairs were never checked because they never shared
a bucket.

### Union-find for clustering

Surviving pairs (above threshold) become edges in a graph. The
implementation uses union-find with path halving and union by size
(`minhash_lsh_impl.hpp:121-131`) to group documents into connected
components.

Clusters are *transitive closures*, not cliques: if A matches B and B
matches C, all three end up in one cluster even if A and C are below
threshold when compared directly.

### The saving

The bucket walk is O(n * b) -- linear in the number of documents. This
replaces the O(n^2) brute-force comparison. The number of Jaccard
verifications depends on how many collisions occur, but for a corpus of
mostly-distinct documents it is far smaller than n^2.

---

## 7. Use case 2: single-document query

**Problem**: given a new document, find its near-duplicates in an index of n
documents.

### Brute force

Compare the query against every indexed document: O(n) Jaccard calls.

### LSH approach

Compute b=24 band keys for the query, then do 24 hash table lookups via
`query_candidates()` at `minhash_lsh_impl.hpp:92-105`:

```cpp
for (uint16_t band = 0; band < num_bands_; ++band) {
  const auto it = bands_[band].find(keys[band]);
  if (it != bands_[band].end()) {
    result.insert(result.end(), it->second.begin(), it->second.end());
  }
}
std::sort(result.begin(), result.end());
result.erase(std::unique(result.begin(), result.end()), result.end());
```

Each lookup is O(1) via `unordered_map::find()`. Only the returned
candidates need Jaccard verification.

From the demo: the query (Fox-A's text) did 24 hash lookups, got candidates
{0, 1}, and verified 2 Jaccard calls. At 1M indexed documents, brute force
would be 1M comparisons; LSH still does 24 lookups plus a handful of
verifications.

---

## 8. Building and running the demo

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make minhash_document_similarity
./minhash/example/minhash_document_similarity
```

The output includes a colour-coded similarity matrix, the band key grid, the
S-curve, and both use cases with side-by-side brute-force vs LSH results.

---

## 9. Source files

| File | Description |
|------|-------------|
| `minhash/include/minhash_sketch.hpp` | Sketch class declaration, builder |
| `minhash/include/minhash_sketch_impl.hpp` | update, merge, jaccard, serialization |
| `minhash/include/minhash_lsh.hpp` | LSH index class declaration |
| `minhash/include/minhash_lsh_impl.hpp` | band_keys, add, query_candidates, get_duplicate_clusters |
| `minhash/example/minhash_document_similarity.cpp` | Runnable demo with the 6-document corpus |
| `minhash/test/minhash_sketch_test.cpp` | Sketch unit tests |
| `minhash/test/minhash_lsh_test.cpp` | LSH index unit tests |

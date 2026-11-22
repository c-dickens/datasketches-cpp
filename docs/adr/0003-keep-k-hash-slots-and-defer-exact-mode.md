<!--
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
-->

# ADR-0003: Format version 1 keeps k-hash slots, and reserves a place for an exact mode

*Status: accepted, 2026-11-22 (MH-22, CD-402). The Linear project "MinHash + LSH" has a copy.*

This decision comes before serialization (MH-23), because after a format exists a change to the slot values or the stored form is expensive. It answers two questions.

1. **Slot algorithm.** Format version 1 keeps the k-hash SplitMix64 expansion of ADR-0002. It does not change to a faster one-hash scheme.
2. **Exact mode.** Version 1 has no exact mode. Its flags byte reserves one bit for a later hash-list form. A version 1 reader rejects a buffer that has this reserved bit set, and also a buffer that has any unassigned flag bit set.

## Evidence

The accuracy numbers come from full runs. The speed numbers come from the MH-18 smoke run on an Apple M4 that was not quiet (load average about 2.8). MH-21 (CD-401) replaces them with full runs later. The decision depends on differences of 8 to 360 times and on a failed band rate, so noise of this size cannot change it.

| Measure, k = 256 unless stated | MinHash (k-hash) | Theta | Source |
| -- | -- | -- | -- |
| Update, `uint64_t` items | 123 ns per item (about 0.47 ns per slot, O(k)) | 2.2 to 6.2 ns per item (about O(1)) | MH-18 smoke |
| Update, k = 16 to 256 | 8 to 56 times slower than theta | | MH-18 smoke |
| `jaccard()` | 23 ns | 8,300 ns (builds a union and an intersection) | MH-18 smoke |
| Accuracy per byte, n = 65536, J = 0.5, k ≥ 64 | baseline | SD × √(theta bytes / MinHash bytes) is 0.96 to 1.06 times the MinHash SD: about the same | MH-17: PR c-dickens/datasketches-cpp#17, section "Finding", from `accuracy_n.tsv` and `accuracy_n_half.tsv` |
| Small sets, k = 4096, n = 100 | 8k bytes, error √(J(1 − J)/k) | exact, 16 + 8n bytes (about 40 times smaller) | MH-17: PR c-dickens/datasketches-cpp#17, section "Finding" |
| LSH band rate J^4: k = 128, 32 bands of r = 4, union of 10 items, J = 0.2 | within ±15% of J^4 over 800,000 band observations | not possible: no aligned slots | MH-6: `minhash/test/minhash_sketch_stats_test.cpp`, "minhash sketch stats: band collision rate"; research §1.2 |

Research document "When to use MinHash rather than theta", §1.3: densified one-permutation hashing (OPH) with rotation, in the fully random model, 4000 trials for each row. The table gives the band collision rate divided by J^r. 1.00 is correct.

| Union size n | OPH, 256 bins, J = 0.5, r = 4 | OPH, 256 bins, J = 0.8, r = 8 |
| -- | -- | -- |
| 16 | 7.25 | 3.92 |
| 64 | 5.59 | 3.52 |
| 256 | 2.55 | 1.83 |
| 1024 | 1.06 | 1.02 |
| 16384 | 0.99 | 1.00 |

## Decision 1: keep k-hash slots

The reason to use MinHash and not theta is LSH: a fixed-length signature whose slots are aligned across sets and independent of each other, at every set size. k-hash MinHash gives the band rate J^r also on very small sets: the MH-6 test "minhash sketch stats: band collision rate" measures J^4 within ±15% at k = 128, r = 4, a union of 10 items and J = 0.2. Theta has no aligned slots.

### Considered options

* **Keep the k-hash SplitMix64 expansion (ADR-0002).** Chosen. The update costs O(k) for each item. In return, `jaccard()` is a slot-by-slot compare, about 360 times faster than theta at k = 256, and LSH verification makes many of these compares.
* **Densified one-permutation hashing.** One hash for each item, so the update is about O(1). Rejected: for sets smaller than about 4 times the signature length, the band rate is 1.8 to 7 times too high. Densification copies a non-empty bin into empty bins, so the bins of a band are not independent. Each bin still collides with probability J, but the band does not collide with probability J^r. This loses the property that MinHash exists for.

### Consequences

* The slot values, the test vectors, `minhash/SPEC.md` and ADR-0002 do not change.
* The update stays O(k). This is the known cost of MinHash against theta. Users who only estimate J of pairs that they choose should use theta.

## Decision 2: defer the exact mode, and keep a place for it in the format

**Fact.** The signature depends only on the set of seeded item hashes. Slot i is the minimum, over all items, of (SplitMix64 output i from s = XXH64(item bytes, length, seed)) >> 1, as `minhash/SPEC.md` ("Slot values") defines it, with the 63-bit range of ADR-0001. So a sketch can keep the seeded 64-bit XXH64 values of its items, without duplicates, while its set is small, and compute the k slot values when it needs them. The result is the same as for a sketch that received the items one at a time. An exact mode therefore changes no slot value and no test vector. It changes only the stored form, the estimator (an exact J when both sketches hold hash lists), and the cost of small sets.

### Considered options

* **Defer it, and reserve a flag bit in format version 1.** Chosen. The exact mode does not change the slot values, so it does not need to come before serialization. It needs only a place in the format.
* **Build it before serialization.** Rejected for now. It changes merge (a union of hash lists, then the change to slots), `jaccard()` and `jaccard_bounds()` when one sketch is exact and the other is not, `band_keys()`, and the sketch copies in the LSH index. All of this would delay the format with no change to the slot values.
* **Never build it.** Rejected. MH-17 shows a large gap on small sets: at k = 4096 and n = 100, MinHash is about 40 times larger than theta, and theta is exact.

### Consequences

* MH-23 (serialization) reserves one flag bit for a hash-list form. A version 1 reader rejects a buffer that has this reserved bit set, because version 1 cannot read a hash list. It also rejects a buffer that has any unassigned flag bit set. A later exact mode then adds a form without breaking version 1 readers or buffers: a version 1 reader refuses the new form, and it never reads it as slots.
* The exact mode is a separate later issue. That issue must keep the slot values that `minhash/SPEC.md` defines, and its tests must show that a sketch built from a hash list has the same signature as a sketch built item by item.
* Until then, the small-set gap is a known limit, and MH-17 measures it. At the same nominal k, the theta ±2σ spread stays smaller than the MinHash spread over the whole n range (`scripts/minhash/results/characterisation/accuracy_n.md`: "> 65536" for every k). With theta at half k (lg_k = log2(K) − 1), `scripts/minhash/results/characterisation/accuracy_n_half.md` gives the end of the small-set region for each k: for example n = 431 to 450 at k = 256, and n = 6889 to 7194 at k = 4096.

## Out of scope

* Optimisation of `update()` that changes no output, for example SIMD over the k steps. The test vectors protect such a change, so it can come at any time.
* A cardinality estimate from the slot values. It needs no change to the format.

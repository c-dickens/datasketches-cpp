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

# ADR-0002: Slots come from a SplitMix64 expansion of one XXH64 hash

*Status: accepted, 2026-11-08 (decision D14). Copied from the Linear working copy.*

Each item is hashed once. Its k slot candidates come from a SplitMix64 stream that starts at that hash:

```
s = XXHash64::hash(bytes(x), len, seed)
for i = 0 … k−1:
    s = s + 0x9E3779B97F4A7C15
    z = (s ^ (s >> 30)) * 0xBF58476D1CE4E5B9
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB
    z =  z ^ (z >> 31)
    slot[i] = min(slot[i], z >> 1)
```

All arithmetic is on unsigned 64-bit values and wraps mod 2^64. Every `>>` is a logical shift (`>>>` in Java). The loop is the reference SplitMix64 generator (Steele, Lea and Flood, 2014), as in Vigna's `splitmix64.c`: from s = 0 its first output is `0xE220A8397B1DCDAF`. Java's `new SplittableRandom(s).nextLong()` is expected to give the same sequence. The test vectors in `minhash/SPEC.md` check the C++ only.

## Evidence

From the research note "MinHash slot hashing", k = 128:

* The earlier double hashing gives bands of 4 that collide 1.99× too often, and bands of 8 109× too often (union 10, J = 0.2). The SplitMix64 expansion gives 0.98× and 0.85× (7 hits, noisy).
* The SplitMix64 expansion passed every slot, pair, triple and band test.
* Cost per update: about 123 ns, against 680–900 ns for k separately seeded XXH64 calls and 69 ns for double hashing.
* hash4j's `MinHashPolicy_v1` (Dynatrace) uses the same scheme.

## Considered options

* **k separately seeded XXH64 calls**, `XXH64(bytes, seed + i) >> 1`. This is the textbook definition, and it needs no constants beyond XXH64. Rejected: it costs 5–7× more at k = 128, its cost grows with item length, and its band tests pass no better.
* **Affine maps** `a_i·h + b_i mod 2^64`. Rejected: only 2-universal, and it needs 2k parameters in the spec.
* **Double hashing** `h1 + i·h2`. Rejected: the LSH defect above.

## Consequences

* Only XXH64 is needed, which every DataSketches language already has, plus three constants and a 5-line loop. Ports copy the constants exactly.
* All k values depend on one 64-bit value. Two items whose XXH64 values collide get the same signature. If two items' starting values differ by j·γ (γ being the step constant), their slot values are shifted by j positions. Each happens with probability about 2^−64 per pair (about k·2^−64 for the shift). `minhash/SPEC.md` states this.
* Like k seeded calls, it has no formal proof of band independence. The band-independence test is the evidence, and it must pass. That test is "minhash sketch stats: band collision rate" in `minhash/test/minhash_sketch_stats_test.cpp`, and it must pass before any LSH code uses the bands.
* Band keys still use one seeded XXH64 call per band. They do not use SplitMix64.
* The seed, the seed hash, the item encoding, the 63-bit range, merge and the estimator do not change.

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

# LSH correctness on 20 Newsgroups

Written by `build/lsh_20news` (`./run_demos.sh`). The ticket is MH-14.

Index: k = 128, 32 bands of 4 slots, threshold 0.5.

## Posts and pairs

- posts: 19997
- indexed posts (posts with at least one shingle): 19925
- posts not indexed (no shingles): 72
- pairs (the pairs of MH-8: all pairs of 2000 sampled posts, seed 1, plus the `Message-ID` pairs): 1999533
- pairs left out (a post is not indexed): 13972
- pairs checked: 1985561

## S-curve: PASS

A pair is a candidate when its band keys are equal in at least one band. In each J bin with at least 1000 pairs, the candidate count must be within expected ± 2.5758 SD. Expected = Σ p_i and SD = √Σ p_i (1 − p_i), with p_i = 1 − (1 − J_i^4)^32.

| J bin | pairs | candidates | expected | SD | candidate rate | expected rate | check |
|---|---:|---:|---:|---:|---:|---:|---|
| [0.0, 0.1) | 1984770 | 0 | 0.23 | 0.48 | 0.000000 | 0.000000 | PASS |
| [0.1, 0.2) | 133 | 2 | 2.05 | 1.41 | 0.015038 | 0.015377 | not checked |
| [0.2, 0.3) | 63 | 10 | 6.47 | 2.38 | 0.158730 | 0.102709 | not checked |
| [0.3, 0.4) | 27 | 10 | 9.70 | 2.45 | 0.370370 | 0.359080 | not checked |
| [0.4, 0.5) | 10 | 6 | 7.07 | 1.41 | 0.600000 | 0.707225 | not checked |
| [0.5, 0.6) | 8 | 8 | 7.31 | 0.79 | 1.000000 | 0.913451 | not checked |
| [0.6, 0.7) | 6 | 6 | 5.96 | 0.19 | 1.000000 | 0.993605 | not checked |
| [0.7, 0.8) | 3 | 3 | 3.00 | 0.01 | 1.000000 | 0.999981 | not checked |
| [0.8, 0.9) | 1 | 1 | 1.00 | 0.00 | 1.000000 | 1.000000 | not checked |
| [0.9, 1.0] | 540 | 540 | 540.00 | 0.00 | 1.000000 | 1.000000 | not checked |

## Cross-posts: PASS

Each pair of indexed posts that share a `Message-ID` must be in one cluster of `get_duplicate_clusters()`.

- pairs checked: 538
- pairs not in one cluster: 0
- clusters: 1648

## Recall and precision

Similar: exact J ≥ 0.5. Verified: candidate and estimated J ≥ 0.5.

- similar pairs: 558
- verified pairs: 557
- both: 555
- recall (both / similar): 0.9946
- precision (both / verified): 0.9964

## The 10 largest buckets

| band | size | first posts |
|---:|---:|---|
| 12 | 16 | `alt.atheism/53061`, `alt.atheism/53115`, `alt.atheism/53149` |
| 11 | 11 | `alt.atheism/53299`, `alt.atheism/53317`, `alt.atheism/53562` |
| 16 | 10 | `talk.politics.mideast/75387`, `talk.politics.mideast/75388`, `talk.politics.mideast/75418` |
| 24 | 9 | `sci.med/58044`, `sci.space/60152`, `talk.politics.guns/53306` |
| 0 | 8 | `talk.politics.misc/178317`, `talk.politics.misc/178330`, `talk.politics.misc/178489` |
| 0 | 7 | `rec.sport.baseball/104712`, `rec.sport.baseball/104713`, `rec.sport.baseball/104714` |
| 1 | 7 | `talk.politics.guns/53305`, `talk.politics.guns/53306`, `talk.politics.mideast/75375` |
| 1 | 7 | `rec.sport.baseball/104712`, `rec.sport.baseball/104713`, `rec.sport.baseball/104714` |
| 2 | 7 | `rec.sport.baseball/104712`, `rec.sport.baseball/104713`, `rec.sport.baseball/104714` |
| 3 | 7 | `rec.sport.baseball/104712`, `rec.sport.baseball/104713`, `rec.sport.baseball/104714` |

## Time (changes from run to run)

LSH covers sketching, `add` and `get_duplicate_clusters()` over all posts. Exact all-pairs covers `news20::jaccard` over all pairs of the 2000 sampled posts. Neither covers loading or shingling.

- LSH, all posts: 0.74 s
- exact all-pairs, sample (T_sample): 6.32 s
- factor N(N − 1) / (n(n − 1)), N = 19997, n = 2000: 100.02
- exact all-pairs, all posts (T_all = T_sample × factor): 631.83 s
- T_all / LSH time: 853.11

Overall: PASS

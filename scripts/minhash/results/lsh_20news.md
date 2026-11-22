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

## S-curve (synthetic sets): PASS

Each pair is 2 synthetic integer sets S1 = [o, o + a + c) and S2 = [o + a, o + 2a + c), with 2a + c = 1000, so exact J = c / 1000. Each bin has 2000 pairs, with J spread across the bin, and no item is in 2 pairs. A pair is a candidate when its band keys are equal in at least one band. The candidate count of a bin is a sum of independent Bernoulli(p_i), with p_i = 1 − (1 − J_i^4)^32. A bin passes when its exact two-sided p-value is at least 1e-06 / 10, so a correct index fails the table by chance with a probability of at most 1e-06.

| J bin | pairs | candidates | expected | candidate rate | expected rate | p-value | check |
|---|---:|---:|---:|---:|---:|---:|---|
| [0.0, 0.1) | 2000 | 2 | 1.22 | 0.001000 | 0.000608 | 0.686 | PASS |
| [0.1, 0.2) | 2000 | 43 | 38.19 | 0.021500 | 0.019097 | 0.472 | PASS |
| [0.2, 0.3) | 2000 | 268 | 246.53 | 0.134000 | 0.123267 | 0.151 | PASS |
| [0.3, 0.4) | 2000 | 728 | 769.29 | 0.364000 | 0.384646 | 0.0551 | PASS |
| [0.4, 0.5) | 2000 | 1451 | 1456.78 | 0.725500 | 0.728392 | 0.784 | PASS |
| [0.5, 0.6) | 2000 | 1890 | 1889.52 | 0.945000 | 0.944760 | 1 | PASS |
| [0.6, 0.7) | 2000 | 1989 | 1993.44 | 0.994500 | 0.996722 | 0.139 | PASS |
| [0.7, 0.8) | 2000 | 2000 | 1999.95 | 1.000000 | 0.999974 | 1 | PASS |
| [0.8, 0.9) | 2000 | 2000 | 2000.00 | 1.000000 | 1.000000 | 1 | PASS |
| [0.9, 1.0] | 2000 | 2000 | 2000.00 | 1.000000 | 1.000000 | 1 | PASS |

## Posts and pairs

- posts: 19997
- indexed posts (posts with at least one shingle): 19925
- posts not indexed (no shingles): 72
- pairs (the pairs of MH-8: all pairs of 2000 sampled posts, seed 1, plus the `Message-ID` pairs): 1999533
- pairs left out (a post is not indexed): 13972
- pairs checked: 1985561

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

## False collisions

Candidates among the pairs with exact J = 0. The band keys of such a pair are equal only when 2 different 64-bit hashes collide.

- pairs with exact J = 0: 1979373
- candidates among them: 0

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

Overall: PASS

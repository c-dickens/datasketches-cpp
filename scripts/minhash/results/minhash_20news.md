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

# MinHash correctness on 20 Newsgroups

Written by `build/minhash_20news` (`./run_demos.sh`). The ticket is MH-8.

## Pairs

- posts: 19997
- sampled pairs (all pairs of 2000 posts, seed 1): 1999000
- `Message-ID` pairs (posts that share a `Message-ID` header): 538
- total pairs (the union, without duplicates): 1999533
- pairs with exact J = 0, left out of the bins: 1993324
- pairs with exact J = 1, left out of the bins: 561

## Criteria

Each J bin [0.1 b, 0.1 (b + 1)) with at least 1000 pairs is checked. A bin with fewer pairs is reported as "not checked".

- bias: |mean(estimate - exact)| <= 0.01
- spread: |observed SD / expected SD - 1| <= 0.15, with expected SD = sqrt(mean(J (1 - J)) / k)
- coverage 2 SD: `jaccard_bounds(b, 2)` covers the exact J for at least 0.9445 of the pairs
- coverage 3 SD: `jaccard_bounds(b, 3)` covers the exact J for at least 0.9873 of the pairs

## k = 64: PASS

| J bin | pairs | mean bias | observed SD | expected SD | SD ratio | coverage 2 SD | coverage 3 SD | bias | spread | coverage 2 SD | coverage 3 SD |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| [0.0, 0.1) | 5397 | 0.00041 | 0.01106 | 0.01085 | 1.020 | 0.9917 | 1.0000 | PASS | PASS | PASS | PASS |
| [0.1, 0.2) | 133 | -0.00454 | 0.04808 | 0.04336 | 1.109 | 0.9549 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.2, 0.3) | 63 | 0.01436 | 0.05358 | 0.05308 | 1.009 | 0.9365 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.3, 0.4) | 27 | 0.00453 | 0.06027 | 0.05918 | 1.018 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.4, 0.5) | 10 | 0.02170 | 0.06410 | 0.06198 | 1.034 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.5, 0.6) | 8 | -0.01041 | 0.05541 | 0.06237 | 0.888 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.6, 0.7) | 6 | -0.01468 | 0.03352 | 0.06012 | 0.557 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.7, 0.8) | 3 | 0.01664 | 0.04030 | 0.05370 | 0.751 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.8, 0.9) | 1 | -0.05194 | 0.00000 | 0.04279 | 0.000 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.9, 1.0) | 0 | - | - | - | - | - | - | not checked | not checked | not checked | not checked |

## k = 128: PASS

| J bin | pairs | mean bias | observed SD | expected SD | SD ratio | coverage 2 SD | coverage 3 SD | bias | spread | coverage 2 SD | coverage 3 SD |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| [0.0, 0.1) | 5397 | 0.00075 | 0.00836 | 0.00767 | 1.091 | 0.9843 | 0.9998 | PASS | PASS | PASS | PASS |
| [0.1, 0.2) | 133 | 0.00057 | 0.03031 | 0.03066 | 0.989 | 0.9549 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.2, 0.3) | 63 | 0.00754 | 0.03774 | 0.03753 | 1.006 | 0.9524 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.3, 0.4) | 27 | 0.00713 | 0.03821 | 0.04185 | 0.913 | 0.9259 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.4, 0.5) | 10 | -0.01034 | 0.04423 | 0.04383 | 1.009 | 0.9000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.5, 0.6) | 8 | -0.01431 | 0.04112 | 0.04410 | 0.933 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.6, 0.7) | 6 | 0.00615 | 0.02860 | 0.04251 | 0.673 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.7, 0.8) | 3 | 0.00362 | 0.03674 | 0.03797 | 0.968 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.8, 0.9) | 1 | 0.00275 | 0.00000 | 0.03026 | 0.000 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.9, 1.0) | 0 | - | - | - | - | - | - | not checked | not checked | not checked | not checked |

## k = 256: PASS

| J bin | pairs | mean bias | observed SD | expected SD | SD ratio | coverage 2 SD | coverage 3 SD | bias | spread | coverage 2 SD | coverage 3 SD |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| [0.0, 0.1) | 5397 | 0.00020 | 0.00556 | 0.00542 | 1.025 | 0.9852 | 0.9993 | PASS | PASS | PASS | PASS |
| [0.1, 0.2) | 133 | -0.00049 | 0.02060 | 0.02168 | 0.950 | 0.9850 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.2, 0.3) | 63 | 0.00493 | 0.02627 | 0.02654 | 0.990 | 0.9683 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.3, 0.4) | 27 | 0.00641 | 0.02848 | 0.02959 | 0.963 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.4, 0.5) | 10 | -0.01307 | 0.01944 | 0.03099 | 0.627 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.5, 0.6) | 8 | -0.02017 | 0.03326 | 0.03118 | 1.066 | 0.8750 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.6, 0.7) | 6 | 0.00290 | 0.00694 | 0.03006 | 0.231 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.7, 0.8) | 3 | 0.01013 | 0.02207 | 0.02685 | 0.822 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.8, 0.9) | 1 | 0.01447 | 0.00000 | 0.02140 | 0.000 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.9, 1.0) | 0 | - | - | - | - | - | - | not checked | not checked | not checked | not checked |

## k = 512: PASS

| J bin | pairs | mean bias | observed SD | expected SD | SD ratio | coverage 2 SD | coverage 3 SD | bias | spread | coverage 2 SD | coverage 3 SD |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| [0.0, 0.1) | 5397 | 0.00008 | 0.00417 | 0.00384 | 1.087 | 0.9724 | 0.9967 | PASS | PASS | PASS | PASS |
| [0.1, 0.2) | 133 | -0.00173 | 0.01570 | 0.01533 | 1.024 | 0.9699 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.2, 0.3) | 63 | 0.00165 | 0.02083 | 0.01877 | 1.110 | 0.9524 | 0.9841 | not checked | not checked | not checked | not checked |
| [0.3, 0.4) | 27 | 0.00308 | 0.01954 | 0.02092 | 0.934 | 0.9630 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.4, 0.5) | 10 | 0.00119 | 0.02376 | 0.02191 | 1.084 | 0.9000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.5, 0.6) | 8 | -0.00699 | 0.01872 | 0.02205 | 0.849 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.6, 0.7) | 6 | -0.00817 | 0.01775 | 0.02126 | 0.835 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.7, 0.8) | 3 | 0.00492 | 0.00204 | 0.01898 | 0.108 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.8, 0.9) | 1 | 0.02228 | 0.00000 | 0.01513 | 0.000 | 1.0000 | 1.0000 | not checked | not checked | not checked | not checked |
| [0.9, 1.0) | 0 | - | - | - | - | - | - | not checked | not checked | not checked | not checked |

## Merge

Each post's sorted shingle set is split into 4 consecutive pieces. The merge of the piece sketches must equal the sketch of the whole set, at every k.

- posts checked: 19997 at each of 4 values of k
- mismatches: 0
- result: PASS

Overall: PASS

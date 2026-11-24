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

# Trajectory deduplication: candidate items on exact Jaccard

Written by `uv run scripts/minhash/analysis/trajectory_dedup.py`. Exact Jaccard for all pairs of the MH-15 sample. The findings and the trace examples are in `scripts/minhash/analysis/trajectory_dedup_findings.md`.

## 1. Sample

- Trajectories: 11166. Task instances: 1000. Repos: 579. Pairs: 62,334,195; same task 65,505.
- Distinct issue texts: 997. Task instances that share one issue text: borgbackup__borg-6527, borgbackup__borg-6528, borgbackup__borg-6529; tornadoweb__tornado-2581, tornadoweb__tornado-2589. The cluster tables use the issue text as truth.

## 2. Candidates: exact J by pair class

Same task: two trajectories of one task instance. Same repo: same repo, other task instance. Other repo: every other pair (exact, all pairs).
AUC: same task against same repo. t: the 0.05-grid J that maximises TPR − FPR for same task against same repo.

| Item | median size (empty) | same task mean / median | ≥ 0.3 / 0.5 / 0.7 | same repo mean | ≥ 0.3 / 0.5 | other repo mean | ≥ 0.3 | AUC | t |
|---|---|---|---|---|---|---|---|---|---|
| words_w1 | 483 (0) | 0.492 / 0.494 | 0.999 / 0.444 / 0.000 | 0.324 | 0.7431 / 0.0018 | 0.268 | 0.12430 | 0.994 | 0.40 |
| words_w1_df50 | 286 (0) | 0.349 / 0.349 | 0.830 / 0.005 / 0.000 | 0.141 | 0.0036 / 0.0000 | 0.086 | 0.00000 | 0.997 | 0.25 |
| words_w1_df20 | 181 (0) | 0.320 / 0.317 | 0.616 / 0.007 / 0.000 | 0.095 | 0.0030 / 0.0000 | 0.032 | 0.00000 | 0.997 | 0.20 |
| words_w1_df5 | 92 (0) | 0.321 / 0.315 | 0.579 / 0.028 / 0.000 | 0.062 | 0.0034 / 0.0000 | 0.008 | 0.00000 | 0.996 | 0.20 |
| words_w1_df20_min2 | 179 (0) | 0.324 / 0.321 | 0.642 / 0.009 / 0.000 | 0.096 | 0.0031 / 0.0000 | 0.032 | 0.00000 | 0.997 | 0.20 |
| words_w1_df2_min2 | 55 (0) | 0.348 / 0.341 | 0.664 / 0.084 / 0.003 | 0.042 | 0.0040 / 0.0001 | 0.003 | 0.00000 | 0.996 | 0.20 |
| nocode_words | 123 (0) | 0.504 / 0.503 | 0.950 / 0.510 / 0.069 | 0.235 | 0.1014 / 0.0025 | 0.169 | 0.00061 | 0.977 | 0.35 |
| nocode_words_df20_min2 | 64 (0) | 0.427 / 0.415 | 0.754 / 0.330 / 0.063 | 0.092 | 0.0056 / 0.0016 | 0.022 | 0.00000 | 0.988 | 0.20 |
| lines | 33 (0) | 0.285 / 0.273 | 0.394 / 0.034 / 0.001 | 0.111 | 0.0033 / 0.0001 | 0.084 | 0.00000 | 0.972 | 0.20 |
| lines_df20_min2 | 16 (0) | 0.337 / 0.324 | 0.596 / 0.106 / 0.005 | 0.053 | 0.0036 / 0.0001 | 0.006 | 0.00000 | 0.990 | 0.20 |
| opening | 17 (0) | 0.330 / 0.310 | 0.540 / 0.116 / 0.012 | 0.146 | 0.0170 / 0.0007 | 0.084 | 0.00025 | 0.923 | 0.20 |
| opening_df20_min2 | 9 (0) | 0.357 / 0.333 | 0.601 / 0.225 / 0.040 | 0.064 | 0.0155 / 0.0015 | 0.006 | 0.00000 | 0.957 | 0.20 |
| paths | 11 (0) | 0.609 / 0.600 | 0.982 / 0.774 / 0.290 | 0.259 | 0.3341 / 0.0619 | 0.099 | 0.00388 | 0.951 | 0.40 |
| paths_df20_min2 | 8 (0) | 0.582 / 0.571 | 0.959 / 0.708 / 0.256 | 0.159 | 0.1555 / 0.0309 | 0.004 | 0.00000 | 0.967 | 0.35 |
| args | 35 (0) | 0.380 / 0.368 | 0.764 / 0.143 / 0.008 | 0.135 | 0.0139 / 0.0003 | 0.072 | 0.00003 | 0.985 | 0.25 |
| args_df20_min2 | 23 (0) | 0.392 / 0.381 | 0.781 / 0.193 / 0.013 | 0.082 | 0.0069 / 0.0001 | 0.004 | 0.00000 | 0.992 | 0.20 |
| edits | 52 (179) | 0.366 / 0.304 | 0.506 / 0.310 / 0.163 | 0.003 | 0.0026 / 0.0009 | 0.000 | 0.00000 | 0.938 | 0.20 |
| edits_min2 | 38 (252) | 0.487 / 0.493 | 0.676 / 0.498 / 0.303 | 0.005 | 0.0037 / 0.0015 | 0.000 | 0.00000 | 0.938 | 0.20 |
| paths_edits | 64 (0) | 0.390 / 0.354 | 0.577 / 0.328 / 0.138 | 0.039 | 0.0035 / 0.0010 | 0.016 | 0.00000 | 0.974 | 0.20 |
| paths_edits_df20 | 61 (0) | 0.375 / 0.335 | 0.549 / 0.310 / 0.130 | 0.021 | 0.0030 / 0.0009 | 0.001 | 0.00000 | 0.983 | 0.20 |
| paths_edits_min2 | 50 (0) | 0.506 / 0.519 | 0.751 / 0.527 / 0.269 | 0.053 | 0.0053 / 0.0016 | 0.021 | 0.00000 | 0.982 | 0.20 |
| paths_edits_df20_min2 | 47 (0) | 0.491 / 0.500 | 0.726 / 0.507 / 0.257 | 0.028 | 0.0043 / 0.0015 | 0.001 | 0.00000 | 0.988 | 0.20 |
| diffedits_min2 | 6 (534) | 0.319 / 0.236 | 0.421 / 0.266 / 0.144 | 0.001 | 0.0006 / 0.0002 | 0.000 | 0.00000 | 0.882 | 0.20 |
| paths_diffedits_df20_min2 | 15 (0) | 0.423 / 0.400 | 0.716 / 0.343 / 0.088 | 0.073 | 0.0124 / 0.0007 | 0.002 | 0.00000 | 0.977 | 0.20 |
| core | 89 (0) | 0.345 / 0.326 | 0.553 / 0.209 / 0.032 | 0.052 | 0.0024 / 0.0002 | 0.026 | 0.00000 | 0.982 | 0.20 |
| core_df20_min2 | 62 (0) | 0.432 / 0.434 | 0.716 / 0.392 / 0.100 | 0.034 | 0.0031 / 0.0005 | 0.002 | 0.00000 | 0.993 | 0.20 |

## 3. Clusters on exact J (connected components of the pairs with J ≥ t)

Truth: the issue text (997 issues). Kept: components, that is the trajectories that deduplication keeps. Merged: components with more than one issue. Split: issues in more than one component. Cross pairs: pairs of different issues with J ≥ t. The row of each item is its t with the highest ARI.

| Item | t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |
|---|---|---|---|---|---|---|---|---|---|
| words_w1 | 0.45 | 1185 | 0.916 | 0.974 | 17 | 131 | 200 | 128 | 170 |
| words_w1_df50 | 0.35 | 2506 | 0.876 | 0.999 | 2 | 500 | 1404 | 48 | 12 |
| words_w1_df20 | 0.25 | 1055 | 0.976 | 0.983 | 19 | 60 | 79 | 48 | 307 |
| words_w1_df5 | 0.25 | 1224 | 0.962 | 0.984 | 16 | 137 | 228 | 48 | 357 |
| words_w1_df20_min2 | 0.25 | 1031 | 0.972 | 0.980 | 21 | 48 | 57 | 48 | 336 |
| words_w1_df2_min2 | 0.25 | 1178 | 0.946 | 0.971 | 28 | 107 | 200 | 48 | 611 |
| nocode_words | 0.45 | 1883 | 0.854 | 0.971 | 29 | 443 | 777 | 114 | 343 |
| nocode_words_df20_min2 | 0.35 | 1927 | 0.897 | 0.984 | 23 | 460 | 775 | 48 | 359 |
| lines | 0.35 | 5984 | 0.569 | 0.982 | 26 | 794 | 5128 | 47 | 359 |
| lines_df20_min2 | 0.35 | 2074 | 0.862 | 0.972 | 36 | 378 | 932 | 36 | 387 |
| opening | 0.45 | 6377 | 0.489 | 0.985 | 29 | 883 | 5298 | 35 | 355 |
| opening_df20_min2 | 0.45 | 3510 | 0.675 | 0.966 | 62 | 682 | 2072 | 104 | 525 |
| paths | 0.75 | 5510 | 0.563 | 0.963 | 45 | 864 | 4423 | 58 | 1074 |
| paths_df20_min2 | 0.65 | 3280 | 0.664 | 0.941 | 55 | 651 | 2116 | 134 | 1617 |
| args | 0.40 | 3297 | 0.760 | 0.956 | 40 | 611 | 2310 | 70 | 520 |
| args_df20_min2 | 0.35 | 1243 | 0.893 | 0.945 | 49 | 167 | 268 | 77 | 533 |
| edits | 0.20 | 2379 | 0.859 | 0.986 | 27 | 568 | 1183 | 48 | 608 |
| edits_min2 | 0.20 | 1475 | 0.905 | 0.967 | 45 | 338 | 351 | 48 | 972 |
| paths_edits | 0.30 | 2765 | 0.817 | 0.984 | 25 | 632 | 1549 | 54 | 493 |
| paths_edits_df20 | 0.25 | 2399 | 0.849 | 0.984 | 26 | 579 | 1191 | 55 | 528 |
| paths_edits_min2 | 0.35 | 1479 | 0.896 | 0.976 | 39 | 367 | 250 | 56 | 553 |
| paths_edits_df20_min2 | 0.30 | 1406 | 0.906 | 0.973 | 41 | 337 | 214 | 48 | 650 |
| diffedits_min2 | 0.20 | 2013 | 0.867 | 0.991 | 22 | 521 | 724 | 48 | 156 |
| paths_diffedits_df20_min2 | 0.45 | 2698 | 0.753 | 0.979 | 21 | 526 | 1240 | 119 | 162 |
| core | 0.25 | 2098 | 0.860 | 0.976 | 26 | 479 | 972 | 76 | 476 |
| core_df20_min2 | 0.30 | 1364 | 0.932 | 0.985 | 24 | 288 | 207 | 48 | 429 |

### words_w1 at each t

| t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |
|---|---|---|---|---|---|---|---|---|
| 0.35 | 108 | 0.000 | 0.092 | 4 | 5 | 5 | 10170 | 167301 |
| 0.40 | 791 | 0.096 | 0.780 | 56 | 17 | 17 | 1550 | 4979 |
| 0.45 | 1185 | 0.916 | 0.974 | 17 | 131 | 200 | 128 | 170 |
| 0.50 | 2970 | 0.849 | 1.000 | 1 | 585 | 1894 | 47 | 5 |
| 0.55 | 7888 | 0.365 | 1.000 | 0 | 949 | 7096 | 27 | 0 |
| 0.60 | 10758 | 0.034 | 1.000 | 0 | 996 | 10581 | 20 | 0 |

### words_w1_df20_min2 at each t

| t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |
|---|---|---|---|---|---|---|---|---|
| 0.20 | 916 | 0.651 | 0.923 | 44 | 5 | 4 | 356 | 1629 |
| 0.25 | 1031 | 0.972 | 0.980 | 21 | 48 | 57 | 48 | 336 |
| 0.30 | 1693 | 0.938 | 0.996 | 6 | 314 | 633 | 48 | 109 |
| 0.35 | 4290 | 0.730 | 0.999 | 2 | 735 | 3199 | 47 | 37 |
| 0.40 | 7766 | 0.362 | 1.000 | 1 | 938 | 6960 | 26 | 4 |
| 0.45 | 10034 | 0.102 | 1.000 | 0 | 984 | 9632 | 18 | 0 |
| 0.50 | 10844 | 0.029 | 1.000 | 0 | 994 | 10730 | 12 | 0 |
| 0.55 | 11070 | 0.007 | 1.000 | 0 | 997 | 11028 | 10 | 0 |
| 0.60 | 11146 | 0.001 | 1.000 | 0 | 997 | 11133 | 6 | 0 |

### paths_edits_df20_min2 at each t

| t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |
|---|---|---|---|---|---|---|---|---|
| 0.20 | 1065 | 0.744 | 0.919 | 60 | 162 | 66 | 234 | 1377 |
| 0.25 | 1214 | 0.891 | 0.957 | 47 | 239 | 115 | 94 | 887 |
| 0.30 | 1406 | 0.906 | 0.973 | 41 | 337 | 214 | 48 | 650 |
| 0.35 | 1617 | 0.898 | 0.986 | 30 | 415 | 323 | 48 | 440 |
| 0.40 | 1888 | 0.874 | 0.992 | 21 | 504 | 471 | 48 | 333 |
| 0.45 | 2263 | 0.830 | 0.994 | 15 | 597 | 739 | 48 | 251 |
| 0.50 | 2668 | 0.788 | 0.996 | 12 | 673 | 1048 | 48 | 212 |
| 0.55 | 3196 | 0.731 | 0.996 | 11 | 750 | 1483 | 46 | 146 |
| 0.60 | 3782 | 0.670 | 0.997 | 10 | 804 | 2015 | 38 | 102 |

## 4. Near-duplicates: pairs with very high exact J

| Item | same task ≥ 0.8 | same task ≥ 0.9 | other task ≥ 0.8 | other task ≥ 0.9 |
|---|---|---|---|---|
| words_w1 | 1 | 0 | 0 | 0 |
| words_w1_df50 | 1 | 0 | 0 | 0 |
| words_w1_df20 | 3 | 0 | 0 | 0 |
| words_w1_df5 | 3 | 3 | 0 | 0 |
| words_w1_df20_min2 | 3 | 0 | 0 | 0 |
| words_w1_df2_min2 | 18 | 3 | 0 | 0 |
| nocode_words | 635 | 4 | 2 | 0 |
| nocode_words_df20_min2 | 1122 | 129 | 2 | 0 |
| lines | 3 | 0 | 0 | 0 |
| lines_df20_min2 | 55 | 13 | 0 | 0 |
| opening | 191 | 24 | 0 | 0 |
| opening_df20_min2 | 930 | 205 | 7 | 1 |
| paths | 8766 | 2726 | 659 | 174 |
| paths_df20_min2 | 8553 | 2957 | 575 | 362 |
| args | 95 | 9 | 0 | 0 |
| args_df20_min2 | 213 | 36 | 0 | 0 |
| edits | 7674 | 5171 | 30 | 18 |
| edits_min2 | 14129 | 8561 | 57 | 32 |
| paths_edits | 4622 | 1164 | 20 | 2 |
| paths_edits_df20 | 4418 | 1287 | 21 | 3 |
| paths_edits_min2 | 9414 | 2562 | 47 | 8 |
| paths_edits_df20_min2 | 9093 | 2831 | 44 | 7 |
| diffedits_min2 | 7809 | 6577 | 13 | 6 |
| paths_diffedits_df20_min2 | 2375 | 646 | 3 | 1 |
| core | 485 | 16 | 0 | 0 |
| core_df20_min2 | 1996 | 197 | 0 | 0 |

## 5. The same clusters from MinHash estimates

Signatures: k random permutations of the items (numpy `default_rng`, seeds 1 to 3), estimate = share of equal slots, for every pair with exact J ≥ 0.10. Pairs below 0.10 are not estimated; the last column is their expected number of estimates ≥ t under the binomial model (the sum over the 0.01 bins of the count × P(Bin(k, J) ≥ tk)).

| Item | k | seed | t | kept | ARI | purity | merged | split | largest | cross pairs | expected extra from J < 0.10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| words_w1_df20_min2 | 128 | 1 | 0.20 | 708 | 0.237 | 0.724 | 80 | 2 | 701 | 6592 | 22.7 |
| words_w1_df20_min2 | 128 | 1 | 0.25 | 955 | 0.601 | 0.920 | 37 | 33 | 396 | 945 | 0.0 |
| words_w1_df20_min2 | 128 | 1 | 0.30 | 1520 | 0.931 | 0.981 | 19 | 250 | 52 | 175 | 0.0 |
| words_w1_df20_min2 | 128 | 2 | 0.20 | 768 | 0.340 | 0.785 | 88 | 1 | 595 | 2846 | 22.7 |
| words_w1_df20_min2 | 128 | 2 | 0.25 | 1001 | 0.862 | 0.943 | 38 | 57 | 116 | 579 | 0.0 |
| words_w1_df20_min2 | 128 | 2 | 0.30 | 1665 | 0.936 | 0.989 | 14 | 301 | 48 | 120 | 0.0 |
| words_w1_df20_min2 | 128 | 3 | 0.20 | 774 | 0.388 | 0.788 | 79 | 3 | 488 | 4781 | 22.7 |
| words_w1_df20_min2 | 128 | 3 | 0.25 | 982 | 0.704 | 0.934 | 32 | 47 | 298 | 792 | 0.0 |
| words_w1_df20_min2 | 128 | 3 | 0.30 | 1673 | 0.933 | 0.988 | 13 | 327 | 48 | 174 | 0.0 |
| words_w1_df20_min2 | 256 | 1 | 0.20 | 840 | 0.460 | 0.851 | 66 | 6 | 475 | 2511 | 0.0 |
| words_w1_df20_min2 | 256 | 1 | 0.25 | 997 | 0.914 | 0.960 | 30 | 34 | 110 | 469 | 0.0 |
| words_w1_df20_min2 | 256 | 1 | 0.30 | 1529 | 0.945 | 0.991 | 11 | 271 | 48 | 126 | 0.0 |
| words_w1_df20_min2 | 256 | 2 | 0.20 | 851 | 0.403 | 0.861 | 59 | 3 | 572 | 2140 | 0.0 |
| words_w1_df20_min2 | 256 | 2 | 0.25 | 1003 | 0.943 | 0.964 | 32 | 40 | 57 | 437 | 0.0 |
| words_w1_df20_min2 | 256 | 2 | 0.30 | 1618 | 0.943 | 0.993 | 7 | 277 | 48 | 111 | 0.0 |
| words_w1_df20_min2 | 256 | 3 | 0.20 | 858 | 0.493 | 0.869 | 59 | 3 | 447 | 2415 | 0.0 |
| words_w1_df20_min2 | 256 | 3 | 0.25 | 1006 | 0.937 | 0.962 | 30 | 43 | 68 | 453 | 0.0 |
| words_w1_df20_min2 | 256 | 3 | 0.30 | 1604 | 0.946 | 0.994 | 10 | 300 | 48 | 122 | 0.0 |
| paths_edits_df20_min2 | 128 | 1 | 0.25 | 1205 | 0.860 | 0.949 | 49 | 239 | 123 | 886 | 0.0 |
| paths_edits_df20_min2 | 128 | 1 | 0.30 | 1408 | 0.918 | 0.979 | 38 | 330 | 48 | 579 | 0.0 |
| paths_edits_df20_min2 | 128 | 1 | 0.35 | 1602 | 0.902 | 0.986 | 30 | 413 | 48 | 438 | 0.0 |
| paths_edits_df20_min2 | 128 | 2 | 0.25 | 1188 | 0.820 | 0.946 | 54 | 226 | 181 | 977 | 0.0 |
| paths_edits_df20_min2 | 128 | 2 | 0.30 | 1387 | 0.914 | 0.975 | 43 | 328 | 48 | 672 | 0.0 |
| paths_edits_df20_min2 | 128 | 2 | 0.35 | 1600 | 0.900 | 0.985 | 34 | 415 | 48 | 520 | 0.0 |
| paths_edits_df20_min2 | 128 | 3 | 0.25 | 1200 | 0.842 | 0.950 | 49 | 237 | 154 | 948 | 0.0 |
| paths_edits_df20_min2 | 128 | 3 | 0.30 | 1381 | 0.910 | 0.973 | 46 | 334 | 48 | 619 | 0.0 |
| paths_edits_df20_min2 | 128 | 3 | 0.35 | 1593 | 0.904 | 0.986 | 31 | 408 | 48 | 436 | 0.0 |
| paths_edits_df20_min2 | 256 | 1 | 0.25 | 1215 | 0.876 | 0.953 | 48 | 247 | 100 | 875 | 0.0 |
| paths_edits_df20_min2 | 256 | 1 | 0.30 | 1393 | 0.913 | 0.975 | 42 | 332 | 48 | 613 | 0.0 |
| paths_edits_df20_min2 | 256 | 1 | 0.35 | 1616 | 0.901 | 0.988 | 29 | 420 | 48 | 444 | 0.0 |
| paths_edits_df20_min2 | 256 | 2 | 0.25 | 1212 | 0.884 | 0.957 | 47 | 237 | 109 | 908 | 0.0 |
| paths_edits_df20_min2 | 256 | 2 | 0.30 | 1383 | 0.913 | 0.973 | 44 | 329 | 48 | 674 | 0.0 |
| paths_edits_df20_min2 | 256 | 2 | 0.35 | 1606 | 0.898 | 0.985 | 35 | 417 | 48 | 477 | 0.0 |
| paths_edits_df20_min2 | 256 | 3 | 0.25 | 1207 | 0.874 | 0.954 | 46 | 245 | 107 | 907 | 0.0 |
| paths_edits_df20_min2 | 256 | 3 | 0.30 | 1384 | 0.912 | 0.973 | 44 | 334 | 48 | 641 | 0.0 |
| paths_edits_df20_min2 | 256 | 3 | 0.35 | 1613 | 0.899 | 0.985 | 31 | 419 | 48 | 437 | 0.0 |

## 5b. Stop list from half of the task instances

The items above the 20% DF cut are counted on the 500 task instances with an even position in the sorted ids, then dropped from all trajectories. The df_min rule stays on the full corpus: it only removes items no other trajectory has. Clusters on exact J, against the issue text, at each item's t.

| Item | stop list | t | kept | ARI | purity | merged | split | cross pairs |
|---|---|---|---|---|---|---|---|---|
| words_w1_df20_min2 | full corpus | 0.25 | 1031 | 0.972 | 0.980 | 21 | 48 | 336 |
| words_w1_df20_min2 | half (586 items) | 0.25 | 1028 | 0.975 | 0.981 | 21 | 46 | 336 |
| paths_edits_df20_min2 | full corpus | 0.30 | 1406 | 0.906 | 0.973 | 41 | 337 | 650 |
| paths_edits_df20_min2 | half (5 items) | 0.30 | 1406 | 0.906 | 0.973 | 41 | 337 | 650 |

## 6. Solution level

Final patch lines (MH-E1 `patch_no_new`), same-task pairs: mean J 0.246; share ≥ 0.5 0.175, ≥ 0.8 0.096, = 1 0.080.

| Item | mean J, same patch (patch J ≥ 0.8) | mean J, other patch (patch J < 0.2) | AUC |
|---|---|---|---|
| words_w1 | 0.509 | 0.482 | 0.660 |
| words_w1_df20_min2 | 0.340 | 0.313 | 0.625 |
| paths_edits_df20_min2 | 0.725 | 0.392 | 0.851 |
| edits_min2 | 0.823 | 0.360 | 0.874 |
| diffedits_min2 | 0.882 | 0.151 | 0.949 |
| paths_diffedits_df20_min2 | 0.662 | 0.338 | 0.925 |

## 7. What deduplication keeps (words_w1_df20_min2, t = 0.30)

- Components: 1693 for 997 issues. Sizes: 1: 633, 2: 59, 3: 40, 4: 38, 5: 38, 6: 53, 7: 108, 8: 85, 9: 96, 10: 85, 11: 101, 12: 96, 13: 77, 14: 56, 15: 26, 16: 12, 17: 22, 18: 11, 19: 17, 20: 9, 21: 7, 22: 8, 23: 2, 24: 4, 28: 3, 29: 2, 30: 1, 31: 2, 33: 1, 48: 1.
- Components with at least one resolved trajectory: 1249 (0.738). Keeping a resolved one where there is one keeps 1249 resolved trajectories.
- Merged components (issues joined by one component):
  - 31: NREL__hescore-hpxml-103 (20), NREL__hescore-hpxml-163 (11)
  - 28: wemake-services__wemake-python-styleguide-1766 (12), wemake-services__wemake-python-styleguide-2500 (16)
  - 19: tobymao__sqlglot-784 (7), tobymao__sqlglot-884 (12)
  - 15: tobymao__sqlglot-3392 (7), tobymao__sqlglot-3552 (8)
  - 14: pypa__pip-12877 (7), pypa__pip-13047 (7)
  - 13: reata__sqllineage-492 (7), reata__sqllineage-494 (6)
- The 10 pairs of different issues with the highest J:
  - J 0.432: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.428: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.411: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.410: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.397: NREL__hescore-hpxml-103 / NREL__hescore-hpxml-163
  - J 0.396: NREL__hescore-hpxml-103 / NREL__hescore-hpxml-163
  - J 0.395: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.393: tobymao__sqlglot-3552 / tobymao__sqlglot-3392
  - J 0.390: NREL__hescore-hpxml-103 / NREL__hescore-hpxml-163
  - J 0.390: tobymao__sqlglot-3552 / tobymao__sqlglot-3392

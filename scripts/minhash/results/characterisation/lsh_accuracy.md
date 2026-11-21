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

# LSH accuracy: the S-curve and the reported rate

Written by `plots/lsh_accuracy.py` from `lsh_accuracy.tsv` (profile `minhash-lsh-accuracy`, MH-19).

## Candidate rate against the theory

Each row has n = Trials and p = TheoryRate. The rows are in three groups:

- normal rows: n p ≥ 20 and n (1 − p) ≥ 20. The z-score
  CandidateZ = (CandidateRate − p) / √(p (1 − p) / n) is valid here.
- small-count rows: 0 < p < 1, but n p < 20 or n (1 − p) < 20. The normal approximation fails,
  so the table gives the smallest exact two-sided binomial p-value. With m rows, a value near 1/m is normal.
- exact rows: p is exactly 0 or 1. NA rows are exact rows where the measured rate differs (expected: 0).

The rows are not independent: every grid point uses the same trials, and each B is a prefix of the next.

| k | rows | normal rows | max abs(CandidateZ) | small-count rows | smallest exact p | exact rows | NA rows |
|---|---|---|---|---|---|---|---|
| 64 | 707 | 213 | 2.183 | 283 | 0.02563 | 211 | 0 |
| 128 | 808 | 195 | 3.203 | 284 | 0.006622 | 329 | 0 |
| 256 | 909 | 177 | 2.872 | 277 | 0.03104 | 455 | 0 |

## Guidance: choose (b, r) for a threshold t

Reported = candidate and estimated J ≥ t. J50: the J where the reported rate first reaches 0.5.
FalsePositiveRate: the reported rate at J = t − 0.1. MissRate: 1 − the reported rate at J = t + 0.1.

| k | b | r | t | J50 | FalsePositiveRate | MissRate |
|---|---|---|---|---|---|---|
| 64 | 1 | 64 | 0.5 | 0.989 | 0.0000 | 1.0000 |
| 64 | 1 | 64 | 0.7 | 0.989 | 0.0000 | 1.0000 |
| 64 | 1 | 64 | 0.9 | 0.989 | 0.0000 | 0.0000 |
| 64 | 2 | 32 | 0.5 | 0.962 | 0.0000 | 1.0000 |
| 64 | 2 | 32 | 0.7 | 0.962 | 0.0000 | 0.9985 |
| 64 | 2 | 32 | 0.9 | 0.962 | 0.0012 | 0.0000 |
| 64 | 4 | 16 | 0.5 | 0.892 | 0.0000 | 0.9993 |
| 64 | 4 | 16 | 0.7 | 0.892 | 0.0005 | 0.8889 |
| 64 | 4 | 16 | 0.9 | 0.908 | 0.0129 | 0.0000 |
| 64 | 8 | 8 | 0.5 | 0.732 | 0.0012 | 0.8811 |
| 64 | 8 | 8 | 0.7 | 0.743 | 0.0256 | 0.2358 |
| 64 | 8 | 8 | 0.9 | 0.896 | 0.0173 | 0.0000 |
| 64 | 16 | 4 | 0.5 | 0.513 | 0.0518 | 0.1282 |
| 64 | 16 | 4 | 0.7 | 0.695 | 0.0554 | 0.0168 |
| 64 | 16 | 4 | 0.9 | 0.896 | 0.0173 | 0.0000 |
| 64 | 32 | 2 | 0.5 | 0.493 | 0.0691 | 0.0352 |
| 64 | 32 | 2 | 0.7 | 0.695 | 0.0554 | 0.0168 |
| 64 | 32 | 2 | 0.9 | 0.896 | 0.0173 | 0.0000 |
| 64 | 64 | 1 | 0.5 | 0.493 | 0.0691 | 0.0352 |
| 64 | 64 | 1 | 0.7 | 0.695 | 0.0554 | 0.0168 |
| 64 | 64 | 1 | 0.9 | 0.896 | 0.0173 | 0.0000 |
| 128 | 1 | 128 | 0.5 | 0.993 | 0.0000 | 1.0000 |
| 128 | 1 | 128 | 0.7 | 0.993 | 0.0000 | 1.0000 |
| 128 | 1 | 128 | 0.9 | 0.993 | 0.0000 | 0.0000 |
| 128 | 2 | 64 | 0.5 | 0.981 | 0.0000 | 1.0000 |
| 128 | 2 | 64 | 0.7 | 0.981 | 0.0000 | 1.0000 |
| 128 | 2 | 64 | 0.9 | 0.981 | 0.0000 | 0.0000 |
| 128 | 4 | 32 | 0.5 | 0.945 | 0.0000 | 1.0000 |
| 128 | 4 | 32 | 0.7 | 0.945 | 0.0000 | 0.9971 |
| 128 | 4 | 32 | 0.9 | 0.945 | 0.0000 | 0.0000 |
| 128 | 8 | 16 | 0.5 | 0.856 | 0.0000 | 0.9980 |
| 128 | 8 | 16 | 0.7 | 0.856 | 0.0002 | 0.7949 |
| 128 | 8 | 16 | 0.9 | 0.903 | 0.0005 | 0.0000 |
| 128 | 16 | 8 | 0.5 | 0.672 | 0.0012 | 0.7683 |
| 128 | 16 | 8 | 0.7 | 0.713 | 0.0059 | 0.0627 |
| 128 | 16 | 8 | 0.9 | 0.901 | 0.0005 | 0.0000 |
| 128 | 32 | 4 | 0.5 | 0.499 | 0.0144 | 0.0198 |
| 128 | 32 | 4 | 0.7 | 0.699 | 0.0081 | 0.0024 |
| 128 | 32 | 4 | 0.9 | 0.901 | 0.0005 | 0.0000 |
| 128 | 64 | 2 | 0.5 | 0.497 | 0.0146 | 0.0090 |
| 128 | 64 | 2 | 0.7 | 0.699 | 0.0081 | 0.0024 |
| 128 | 64 | 2 | 0.9 | 0.901 | 0.0005 | 0.0000 |
| 128 | 128 | 1 | 0.5 | 0.497 | 0.0146 | 0.0090 |
| 128 | 128 | 1 | 0.7 | 0.699 | 0.0081 | 0.0024 |
| 128 | 128 | 1 | 0.9 | 0.901 | 0.0005 | 0.0000 |
| 256 | 1 | 256 | 0.5 | 0.995 | 0.0000 | 1.0000 |
| 256 | 1 | 256 | 0.7 | 0.995 | 0.0000 | 1.0000 |
| 256 | 1 | 256 | 0.9 | 0.995 | 0.0000 | 0.0000 |
| 256 | 2 | 128 | 0.5 | 0.991 | 0.0000 | 1.0000 |
| 256 | 2 | 128 | 0.7 | 0.991 | 0.0000 | 1.0000 |
| 256 | 2 | 128 | 0.9 | 0.991 | 0.0000 | 0.0000 |
| 256 | 4 | 64 | 0.5 | 0.972 | 0.0000 | 1.0000 |
| 256 | 4 | 64 | 0.7 | 0.972 | 0.0000 | 1.0000 |
| 256 | 4 | 64 | 0.9 | 0.972 | 0.0000 | 0.0000 |
| 256 | 8 | 32 | 0.5 | 0.925 | 0.0000 | 1.0000 |
| 256 | 8 | 32 | 0.7 | 0.925 | 0.0000 | 0.9939 |
| 256 | 8 | 32 | 0.9 | 0.926 | 0.0000 | 0.0000 |
| 256 | 16 | 16 | 0.5 | 0.821 | 0.0000 | 0.9961 |
| 256 | 16 | 16 | 0.7 | 0.821 | 0.0000 | 0.6262 |
| 256 | 16 | 16 | 0.9 | 0.900 | 0.0000 | 0.0000 |
| 256 | 32 | 8 | 0.5 | 0.617 | 0.0000 | 0.5806 |
| 256 | 32 | 8 | 0.7 | 0.704 | 0.0000 | 0.0020 |
| 256 | 32 | 8 | 0.9 | 0.900 | 0.0000 | 0.0000 |
| 256 | 64 | 4 | 0.5 | 0.498 | 0.0000 | 0.0002 |
| 256 | 64 | 4 | 0.7 | 0.701 | 0.0000 | 0.0000 |
| 256 | 64 | 4 | 0.9 | 0.900 | 0.0000 | 0.0000 |
| 256 | 128 | 2 | 0.5 | 0.498 | 0.0000 | 0.0002 |
| 256 | 128 | 2 | 0.7 | 0.701 | 0.0000 | 0.0000 |
| 256 | 128 | 2 | 0.9 | 0.900 | 0.0000 | 0.0000 |
| 256 | 256 | 1 | 0.5 | 0.498 | 0.0000 | 0.0002 |
| 256 | 256 | 1 | 0.7 | 0.701 | 0.0000 | 0.0000 |
| 256 | 256 | 1 | 0.9 | 0.900 | 0.0000 | 0.0000 |

## Plots

- [S-curves at k = 64](lsh_scurve_k64.svg)
- [S-curves at k = 128](lsh_scurve_k128.svg)
- [S-curves at k = 256](lsh_scurve_k256.svg)
- [Reported rate at k = 128](lsh_reported_k128.svg)

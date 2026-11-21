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

# MinHash accuracy against set size, with theta

Written by `plots/accuracy_n.py` from `accuracy_n_half.tsv` (profile `minhash-accuracy-n-half`, MH-17). Theta uses lg_k = log2(K)-1, where K is the MinHash k.

## End of the small-set region

The region starts at the smallest n. It ends at the last n before the theta ±2σ spread (error Q0.97725 − Q0.02275) first becomes larger than the MinHash spread. Equal spreads stay in the region, because at small k the quantiles take few values and often tie. "> n" means that the region does not end inside the n range.

| k | J = 0.1 | J = 0.5 | J = 0.9 |
|---|---|---|---|
| 64 | 112 | 117 | 117 |
| 256 | 431 | 450 | 450 |
| 1024 | 1798 | 1722 | 1722 |
| 4096 | 6889 | 6889 | 7194 |

## Memory of theta A against MinHash

TH_BytesA / MH_Bytes at the points where theta A is in estimation mode (retained < n).

| k | min ratio | max ratio |
|---|---|---|
| 64 | 0.5469 | 0.9844 |
| 256 | 0.5117 | 0.9492 |
| 1024 | 0.5029 | 0.9404 |
| 4096 | 0.5007 | 0.9382 |

- [Absolute error against n, J = 0.1](accuracy_n_half_error_j10.svg)
- [Absolute error against n, J = 0.5](accuracy_n_half_error_j50.svg)
- [Absolute error against n, J = 0.9](accuracy_n_half_error_j90.svg)
- [Coverage of the bounds against n](accuracy_n_half_coverage.svg)
- [Memory of theta A against MinHash](accuracy_n_half_memory.svg)

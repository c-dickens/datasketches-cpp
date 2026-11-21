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

Written by `plots/accuracy_n.py` from `accuracy_n.tsv` (profile `minhash-accuracy-n`, MH-17).

## End of the small-set region

The largest n at which the theta ±2σ spread (error Q0.97725 − Q0.02275) is smaller than the MinHash spread.

| k | J = 0.1 | J = 0.5 | J = 0.9 |
|---|---|---|---|
| 16 | 38968 | 65536 | 62757 |
| 64 | 65536 | 65536 | 65536 |
| 256 | 65536 | 65536 | 65536 |
| 1024 | 65536 | 65536 | 65536 |
| 4096 | 65536 | 65536 | 65536 |

## Memory of theta A against MinHash

TH_BytesA / MH_Bytes at the points where theta A is in estimation mode (retained < n).

| k | min ratio | max ratio |
|---|---|---|
| 16 | 1.1875 | 2.0625 |
| 64 | 1.0469 | 1.9219 |
| 256 | 1.0117 | 1.8867 |
| 1024 | 1.0029 | 1.8779 |
| 4096 | 1.0007 | 1.8757 |

- [Absolute error against n, J = 0.1](accuracy_n_error_j10.svg)
- [Absolute error against n, J = 0.5](accuracy_n_error_j50.svg)
- [Absolute error against n, J = 0.9](accuracy_n_error_j90.svg)
- [Coverage of the bounds against n](accuracy_n_coverage.svg)
- [Memory of theta A against MinHash](accuracy_n_memory.svg)

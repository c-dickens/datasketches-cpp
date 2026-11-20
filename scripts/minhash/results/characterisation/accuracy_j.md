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

# MinHash accuracy against J

Written by `plots/accuracy_j.py` from `accuracy_j.tsv` (profile `minhash-accuracy-j`, MH-16).

| k | max abs(BiasOverSE) | min SDRatio | max SDRatio | min Cover1SD | min Cover2SD | min Cover3SD |
|---|---|---|---|---|---|---|
| 16 | 1.706 | 0.9714 | 1.0168 | 0.7053 | 0.9573 | 0.9973 |
| 64 | 1.629 | 0.9710 | 1.0112 | 0.6855 | 0.9565 | 0.9966 |
| 256 | 2.353 | 0.9839 | 1.0170 | 0.6814 | 0.9524 | 0.9966 |
| 1024 | 2.616 | 0.9825 | 1.0232 | 0.6804 | 0.9524 | 0.9949 |
| 4096 | 4.083 | 0.9819 | 1.0246 | 0.6736 | 0.9497 | 0.9956 |

Nominal coverage: 0.6827 (1 SD), 0.9545 (2 SD), 0.9973 (3 SD).

- [Absolute error against J](accuracy_j_error.svg)
- [Normalised error against J](accuracy_j_normalised.svg)
- [Coverage of jaccard_bounds against J](accuracy_j_coverage.svg)

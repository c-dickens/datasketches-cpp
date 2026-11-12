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

# ADR-0001: Slot values use 63 bits

*Status: accepted, 2026-10-18 (CD-284 review). Copied from the Linear working copy.*

Each slot keeps the minimum of `hash >> 1`, and `INT64_MAX` is the empty marker. A port that compares signed 64-bit values (the Java default) then agrees with C++ on every slot. An unsigned 64-bit comparison would disagree on about 99.8% of slots for a 10-item set.

## Evidence (checked 2026-10-18)

Every DataSketches sketch that compares hash values, or does arithmetic on them, uses 63 bits:

* Theta and tuple: `h1 >> 1`, with `MAX_THETA = INT64_MAX` (`theta_update_sketch_base.hpp:183`, `theta_constants.hpp:36`).
* Bloom filter: `((h0 + i * h1) >> 1) % num_bits` (`bloom_filter_impl.hpp:651`).
* Count-Min: the fix proposed in [TCK #18](https://github.com/apache/datasketches-tck/discussions/18) uses `(h1 >>> 1) % numBuckets` and calls the shift "already our convention".

HLL and CPC only take bits from the hash, so the sign does not affect them. The TCK repository holds serialized fixtures only and no hashing spec.

## Considered options

* Keep 64 bits and specify unsigned comparison (`Long.compareUnsigned` in Java). Rejected: it breaks the project convention to keep one bit.

## Consequences

* The empty flag decides emptiness, never the marker: 2 of the 2^64 hash outputs shift to `INT64_MAX`, so a non-empty sketch can hold the marker in a slot.
* The Phase 3 deserializer rejects a slot that has the top bit set.
* The shift changes every signature value. It lands before any test vectors or recorded demo results.

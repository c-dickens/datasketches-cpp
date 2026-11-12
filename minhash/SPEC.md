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

# MinHash sketch specification

This file is the source of truth for how the MinHash sketch hashes items and
makes slot values. Every language implementation must give the same signature
for the same items, k and seed. The test vectors at the end check this.

## Terms

- **Signature**: the k slot values of one sketch.
- **Slot**: one of the k positions in a signature.
- **Slot value**: the smallest value that a slot received.
- **Empty marker**: the slot value of a slot that received no item.

## Parameters

- **k**: the number of slots, 1 to 65535. The default is 256. k does not have
  to be a power of 2.
- **seed**: an unsigned 64-bit value. The default is 9001 (`DEFAULT_SEED`).
  Two sketches can be merged or compared only if their k and seed are equal.
- **seed hash**: the 16-bit seed hash that every DataSketches sketch uses
  (`compute_seed_hash`). For seed 9001 it is `0x93CC`.

## Items become bytes

The rules are the same as in theta, tuple, HLL and CPC. Every multi-byte value
is written in little-endian order, byte by byte, on every platform.

1. An unsigned integer becomes the signed integer of the same width:
   uint32 → int32, uint16 → int16, uint8 → int8. uint64 keeps its 64 bits.
2. Every integer widens with its sign to int64, and is hashed as the 8
   little-endian bytes of its two's complement value.
3. A float becomes a double. Each double then becomes an int64:
   - 0.0 and −0.0 become 0;
   - every NaN becomes `0x7FF8000000000000` (Java's `Double.doubleToLongBits`);
   - every other double keeps its IEEE 754 bits.

   The int64 is hashed by rule 2. So `update(1.0)` and `update(1)` are
   different items, and `update(0.0)` and `update(0)` are the same item.
4. A string is hashed as its bytes. The caller supplies UTF-8. An empty string
   is ignored: the sketch does not change.
5. Raw bytes are hashed as given.

## Slot values

All arithmetic is on unsigned 64-bit values and wraps mod 2^64. Every `>>` is a
logical shift (`>>>` in Java). See ADR-0001 and ADR-0002 in `docs/adr/`.

```
s = XXH64(bytes, length, seed)
for i = 0 … k−1:
    s = s + 0x9E3779B97F4A7C15
    z = (s ^ (s >> 30)) * 0xBF58476D1CE4E5B9
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB
    z =  z ^ (z >> 31)
    slot[i] = min(slot[i], z >> 1)
```

- XXH64 is the standard 64-bit xxHash. The C++ code uses
  `common/include/xxhash64.h`, which reads its input in little-endian order.
- The loop is the reference SplitMix64 generator (Steele, Lea and Flood, 2014;
  Vigna's `splitmix64.c`). From s = 0, its first three outputs are
  `0xE220A8397B1DCDAF`, `0x6E789E6AA1B965F4` and `0x06C45D188009454F`.
- Each slot value lies in [0, 2^63 − 1]. A port that compares signed 64-bit
  values agrees with a port that compares unsigned values.

## Empty marker and the empty flag

- A new sketch has every slot at the empty marker, `INT64_MAX`
  (`0x7FFFFFFFFFFFFFFF`).
- The sketch also keeps an empty flag. The flag is true until the first item.
- Only the flag decides emptiness, never the marker. Two of the 2^64 values of
  z give `INT64_MAX` after the shift, so a non-empty sketch can hold the marker
  in a slot.

## Collision cases

All k slot values of an item come from one 64-bit value s. So:

1. Two items whose XXH64 values are equal get the same slot values in every
   slot. The probability is about 2^−64 for each pair of items.
2. If the XXH64 values of two items differ by j × `0x9E3779B97F4A7C15` (mod
   2^64), for some j with 0 < j < k, the slot values of one item are the slot
   values of the other, shifted by j slots. The probability is about k × 2^−64
   for each pair of items.

Neither case has a material effect on the estimate. There is no formal proof
that the slots of a band are independent. The band-independence test is the
evidence. That test is not in this repository yet: MH-6 (CD-330) adds it, and
it must pass before any LSH code uses the bands.

## Merge

The merge of two sketches with equal k and seed takes the minimum of each slot.
The merged empty flag is true only if both flags are true. The result is the
same as the sketch of the union of the two streams, and it does not depend on
the order of the merges.

## Estimator

- Two non-empty sketches: the estimate of the Jaccard similarity is
  (number of slots i where slot[i] of A equals slot[i] of B) / k.
- Two empty sketches: 1. One empty and one non-empty sketch: 0.
- The bounds are approximate Clopper-Pearson bounds on the match proportion,
  with num_std_devs = 1, 2 or 3. Other values are rejected.

## Test vectors

k = 8 and seed = 9001. Each row gives the item's bytes (hex, in hash order),
the XXH64 value s, and the 8 slot values of a sketch that received only that
item. The values come from the Python `xxhash` package and a separate
SplitMix64 loop, not from the C++ sketch.
`minhash/test/minhash_sketch_vectors_test.cpp` checks them.

| Item | Bytes | s = XXH64(bytes, 9001) |
|---|---|---|
| int64 0 | `0000000000000000` | `0xD71D64972584A288` |
| int64 1 | `0100000000000000` | `0xEFE8454A6499F311` |
| int64 −1 | `ffffffffffffffff` | `0xCCBB58D2567CAC5C` |
| int64 `INT64_MIN` | `0000000000000080` | `0x2ABDB38C33D3914D` |
| int64 `INT64_MAX` | `ffffffffffffff7f` | `0x7ECC0ACD93A2C6C7` |
| double NaN | `000000000000f87f` | `0x0AF4A7FC74C26FA5` |
| double 1.5 | `000000000000f83f` | `0x5225F787BBE7CDB8` |
| string "a" | `61` | `0x0DFA4919E8948650` |
| string "MinHash" | `4d696e48617368` | `0xEF891A4DC448511B` |
| string "naïve" (UTF-8) | `6e61c3af7665` | `0x2FC4FEEC7B91A894` |

Slot values (slot 0 to slot 7):

```
int64 0          63994435A96EBAE5 3B617E1A355C0F92 47FB59AD21DC0D5C 681DBA163E1F7EED
                 01AD99477E4DC78D 07EFEC7F1A4C5232 4C3C0D0CED1E2E37 6F31AD0D99A092A9
int64 1          3FB4D85440219E25 015603DA2C01CB17 672B154DF53262B7 170466E675D595DB
                 6EAB3E2E008876BA 6E39555F027B5919 0C717A2D4559CE4A 0535E083277429EF
int64 -1         7A62DDE66704545F 63B0887375FA2546 6128CA04C0D92999 24267F23D2D8DB0D
                 638A6AF59157DF85 22B2ADDCAD6FDE01 05B6EA3DB6AA85B0 2D2C1C3D2CED017C
int64 INT64_MIN  06837D7659609CDB 6DD0320E8CA2CE42 504912CA49C094CF 3B70403B416501BB
                 195A683D112A51F6 07C9EDF949BEC243 57B4C522FE8C5A1D 6AD0F1243C22098C
int64 INT64_MAX  3EF877423B7584AC 4FFAC9F4E7DEA686 76DC0B9EE0D90CC7 2B4AAE8BE4B92067
                 0292E5559BB22DB7 7D6CCA34BDD6CA1A 545E42EC0DA8BF78 465A220E2F9BE341
double NaN       7BDA394405A04F5E 58DD75AC4B59BFC2 2959AD051A3FF774 46C203DA750D0EE6
                 68A7A0D1FC406B3C 7D691DC23D1B329B 08440ABBF28542CA 63F236F7534795F4
double 1.5       78A0E05187FDD0B3 32E8A587ECD51A3C 03264B41F00A5A96 20BB71885E50190F
                 12099A528A44ED60 31B633B413601D67 06564CB5252C3953 36B7B62E42B4744A
string "a"       76F3106D751773CD 044635EAEF4D88B1 7B8D147CEB7E83F3 2EEC005CD443811D
                 2B0EC77D91DE4751 60C61DC03F829647 0C556477EEA17559 77182CA4FD70DD0C
string "MinHash" 03879EEC4C4BF67B 2601A647F7FEDD91 2DF46719E7E35E59 41294A5018E2F528
                 01AD5B7F829283C2 70BDDE88ABBEC31D 5E7C6CD564439D76 488EEFE85EEC3F3E
string "naïve"   2BAB49E4316B5E82 08BAF3D8DEBAD5B2 442F11B08137763A 22A11B18E0B461DB
                 7FD760A870286500 4C58E56854994DE5 309B3D148DF76EFA 6A49322A9722B549
```

These items must give the same signature:

- int32 −1, uint32 `0xFFFFFFFF`, uint8 255 and uint64 `0xFFFFFFFFFFFFFFFF`
  give the int64 −1 row.
- double 0.0 and double −0.0 give the int64 0 row.
- float 1.5 gives the double 1.5 row.
- Every NaN gives the double NaN row.

An empty string leaves the sketch empty, with every slot at `INT64_MAX`.

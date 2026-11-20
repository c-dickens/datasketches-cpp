#!/usr/bin/env python3
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Computes the band-key test vectors of minhash/SPEC.md ("Band keys") with the
# Python xxhash package, an XXH64 that is independent of the C++ code. Then it
# checks them against the table in SPEC.md. Exit status 1 means a mismatch.

import pathlib
import re
import struct
import sys

import xxhash

SEED = 9001
# the k = 8 signature of the "int64 0" row in SPEC.md "Test vectors"
SLOTS = [0x63994435A96EBAE5, 0x3B617E1A355C0F92, 0x47FB59AD21DC0D5C, 0x681DBA163E1F7EED,
         0x01AD99477E4DC78D, 0x07EFEC7F1A4C5232, 0x4C3C0D0CED1E2E37, 0x6F31AD0D99A092A9]
SPEC = pathlib.Path(__file__).resolve().parents[3] / "minhash" / "SPEC.md"


def band_keys(num_bands):
    r = len(SLOTS) // num_bands
    return ["%016X" % xxhash.xxh64_intdigest(struct.pack("<%dQ" % r, *SLOTS[j * r:(j + 1) * r]),
                                             seed=SEED + j)
            for j in range(num_bands)]


def main():
    # rows of the SPEC.md table: | num_bands | r | `key` `key` ... |
    spec = {int(m.group(1)): re.findall(r"`([0-9A-F]{16})`", m.group(2))
            for m in re.finditer(r"^\| (\d+) \| \d+ \| (.*) \|$", SPEC.read_text(), re.M)}
    ok = True
    for num_bands in (1, 2, 4, 8):
        keys = band_keys(num_bands)
        match = spec.get(num_bands) == keys
        ok = ok and match
        print(num_bands, " ".join(keys), "OK" if match else "MISMATCH with SPEC.md")
    print("xxhash", xxhash.VERSION, "-", "all 15 band keys match SPEC.md" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

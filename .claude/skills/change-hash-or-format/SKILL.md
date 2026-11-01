---
name: change-hash-or-format
description: Rules for any change to how items are hashed, how slot values or band keys are made, the test vectors, or the serialized format of the MinHash sketch. Other languages and stored data must match these values, so a mistake is expensive. Use before you make such a change.
---

# Change hashing or the format

1. **Follow the spec.** `minhash/SPEC.md` is the source of truth. If the ticket changes a rule, change `SPEC.md` in the same PR, with the formula written out.
2. **Use only the decided building blocks:** `XXHash64::hash` from `common/include/xxhash64.h`, unsigned 64-bit arithmetic that wraps, and `hash >> 1` for 63-bit slot values (ADR-0001). The empty marker is `INT64_MAX`. The empty flag, never the marker, decides emptiness.
3. **Bytes are little-endian.** When values become bytes (item encoding, band keys, serialization), write each byte explicitly. Do not `memcpy` an integer in host byte order.
4. **Item encoding follows theta:** integers widen to int64 and hash as 8 bytes; floats become doubles and go through `canonical_double`; strings hash as their UTF-8 bytes; an empty string is ignored.
5. **Compute the expected test vectors with separate code.** In the test, call `XXHash64::hash` directly on byte arrays that you build by hand, and apply the formula from the spec. Never print values from the implementation and paste them back as the expected values.
6. **Copy the vectors into `SPEC.md`,** so that other languages can check them.
7. **Mark the PR.** The first line of the PR body is: `Changes hashing or format: the owner must review this PR line by line.`
8. **Check all platforms.** The vectors must pass in CI on macOS (libc++), Windows (MSVC) and Ubuntu (libstdc++).

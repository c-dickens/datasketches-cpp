---
name: change-hash-or-format
description: Rules for any change to how items are hashed, how slot values or band keys are made, the test vectors, or the serialized format of the MinHash sketch. Other languages and stored data must match these values, so a mistake is expensive. Use before you make such a change.
---

# Change hashing or the format

1. **Follow the spec.** `minhash/SPEC.md` is the source of truth. If the ticket changes a rule, change `SPEC.md` in the same PR, with the formula written out.
2. **Use only the decided building blocks:** `XXHash64::hash` from `common/include/xxhash64.h`, the SplitMix64 expansion of one XXH64 hash into k slot values (ADR-0002, `docs/adr/0002-slots-use-a-splitmix64-expansion.md`), unsigned 64-bit arithmetic that wraps, and `hash >> 1` for 63-bit slot values (ADR-0001, `docs/adr/0001-slot-values-use-63-bits.md`). The empty marker is `INT64_MAX`. The empty flag, never the marker, decides emptiness.
3. **Bytes are little-endian.**
   - *Hashing:* pass an integer to `XXHash64::hash` from memory, as the bloom filter does (`XXHash64::hash(&value, sizeof(value), seed)`). `xxhash64.h` reads the 8 bytes back as one host-order word, so the hash equals XXH64 of the little-endian bytes on every platform. Do not change `xxhash64.h`: other sketches use it.
   - *Serialization:* when values become stored bytes, write each byte explicitly in little-endian order. Do not `memcpy` an integer in host byte order.
4. **Item encoding follows theta:** integers widen to int64 and hash as 8 bytes; floats become doubles and go through `canonical_double`; strings hash as their UTF-8 bytes; an empty string is ignored.
5. **Compute the expected test vectors with separate code.** In the test, call `XXHash64::hash` directly on byte arrays that you build by hand, and apply the formula from the spec. Never print values from the implementation and paste them back as the expected values.
6. **Copy the vectors into `SPEC.md`,** so that other languages can check them.
7. **Mark the PR.** The first line of the PR body is: `Changes hashing or format: the owner must review this PR line by line.`
8. **Check all platforms.** The vectors must pass in CI on macOS (libc++), Windows (MSVC) and Ubuntu (libstdc++).

---
name: review-minhash
description: Review checklist for a change to the MinHash sketch, the LSH index or the demo scripts in this fork. Use on your own diff before you open a PR, and when you review a PR.
---

# MinHash review checklist

Report only real problems. Give each one a file:line, a severity (Blocker, Major or Minor) and the fix. Do not report style that clang-tidy checks.

**Ticket fit**
- The change does what the ticket's "Done when" list asks, and nothing outside its scope.
- Each "Done when" item has evidence in the PR body.

**Correctness**
- Edge cases: an empty sketch, k = 1, one item, identical sketches, a merge of sketches with different seeds (it must throw), and maximum values.
- Invalid arguments throw `std::invalid_argument`, as in theta.
- A merge stays exact (the element-wise minimum), and its result does not depend on the order.

**Hashing and format** (see `change-hash-or-format`)
- The code matches `minhash/SPEC.md`.
- Test vectors change only when the ticket says so, and no expected value comes from the implementation.
- No `std::` random engine and no `std::hash` in hashing.
- Bytes are explicit little-endian.

**C++ and the library**
- C++11 only in `minhash/`: no `std::make_unique`, generic lambdas, deduced return types or other C++14+ features.
- Every container uses the sketch's allocator, and allocator tests cover new storage.
- No change to theta, tuple or other existing behaviour, and no change to an existing test, unless the ticket says so.
- Every new file has the ASF licence header.

**Tests**
- New behaviour has tests that would fail without the change.
- Statistical tests follow `write-statistical-test`: fixed seeds, and thresholds from theory at 5 SD or more.
- No test is skipped, disabled or loosened.

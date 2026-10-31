---
name: write-statistical-test
description: Rules for any test that checks a probability, an estimate, a variance, the coverage of a bound or a collision rate (for example Jaccard estimates, band collision rates or the LSH S-curve). Use before you write or change such a test.
---

# Write a statistical test

A statistical test must never fail by chance on correct code, and it must fail on broken code.

1. **Fix every seed.** Use a fixed list of seeds, or a fixed base seed with a counter. Never use the time, `std::random_device` or an unseeded engine. Make test data with the library's `XXHash64`, or with a small explicit generator such as SplitMix64 written in the test. Do not use `std::` engines: their output differs between standard libraries.
2. **Take expected values from theory**, never from the code under test. Write the formula in a comment. Examples: matches in k slots ~ Binomial(k, J); a band of r slots collides with probability J^r; P(candidate) = 1 − (1 − J^r)^b.
3. **Set each threshold from the standard deviation.** For a count X ~ Binomial(n, p), the mean is np and the SD is √(np(1−p)). Require |X − np| ≤ 5·SD. Under the normal approximation, this gives a false failure rate of about 6 × 10⁻⁷ for each check. When one test makes many checks, widen the bound, or say in a comment why the total rate stays small. When np is below about 20, use an exact binomial bound (`common/include/binomial_bounds.hpp`, or a direct sum).
4. **Prove that the test can fail.** Make a temporary broken version (for example double hashing instead of independent slots, or a biased estimator). Run the test, and paste the failing output into the PR body. Do not commit the broken version.
5. **Keep it fast.** The whole statistical test file must run in under 30 s in the sanitizer job. Measure it with the ASan build in `CLAUDE.md`, and report the time in the PR.
6. **Tag** the test cases `[minhash][stats]`.

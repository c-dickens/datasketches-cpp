# CLAUDE.md: c-dickens/datasketches-cpp, branch minhash/main

This fork builds a MinHash sketch and an LSH index for Apache DataSketches C++.
The work plan is the Linear project "Apache DataSketches", milestone "MinHash + LSH".

## Rules

- Work only in `c-dickens/datasketches-cpp`. Base every branch on `origin/minhash/main`, and open PRs into `minhash/main`.
- Never push to, or open a PR against, any `apache/*` repository.
- Never merge a PR. The owner merges.
- Library code under `minhash/` is C++11. Use the repo's warning flags, and follow the style of `theta/`.
- Never use `std::` random engines or `std::hash` in hashing. Use `common/include/xxhash64.h`.
- Slot values use 63 bits (`hash >> 1`), and `INT64_MAX` is the empty marker. The empty flag decides emptiness.
- Every new source file starts with the ASF licence header.
- `scripts/minhash/` holds the demos. It is fork-only, and it may use C++17.
- If a ticket leaves a decision open, ask the owner in the chat. In an unattended run, comment the question on the ticket and stop.

## Commands

Build and test:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build build -j
    ctest --test-dir build -R minhash --output-on-failure

Whole library, when a change touches `common/`, `theta/` or `tuple/`:

    ctest --test-dir build --output-on-failure

Sanitizers (clang):

    CC=clang CXX=clang++ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=address
    cmake --build build-asan -j && ctest --test-dir build-asan -R minhash --output-on-failure
    CC=clang CXX=clang++ cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=undefined,float-cast-overflow
    cmake --build build-ubsan -j && ctest --test-dir build-ubsan -R minhash --output-on-failure

clang-tidy (any finding fails CI):

    clang-tidy --config-file=.clang-tidy --warnings-as-errors='*' --header-filter='(^|/)minhash/' -p build <changed minhash .cpp files>

A suppression must name the check and give a reason: `// NOLINT(check-name): reason`.

Licence headers:

    tools/rat-check.sh

CI also runs MSVC and the full gcc matrix. CI is the final check.

## Skills

- `implement-ticket`: implement one Linear ticket, from the ticket to an open PR with evidence.
- `write-statistical-test`: any test of a probability, an estimate or a collision rate.
- `change-hash-or-format`: any change to hashing, band keys, test vectors or the serialized format.
- `review-minhash`: the review checklist. Use it on your own diff before you open a PR.

## Glossary

### Dataset

**Task instance**:
One SWE-rebench problem that agents attempt, identified by its instance ID.
*Avoid*: issue, problem, task

**Trajectory**:
One recorded agent attempt at a task instance.
*Avoid*: run, episode, rollout

**Resolved**:
A trajectory whose result passes the tests of its task instance.
*Avoid*: successful, succeeded, passed

**Unresolved**:
A trajectory that is not resolved.
*Avoid*: failed, unsuccessful (keep "failed" for tool errors inside a trajectory)

### Sketch

**Signature**:
The k slot values of one MinHash sketch.
*Avoid*: fingerprint, MinHash vector

**Slot**:
One of the k positions in a signature.
*Avoid*: hash, register, bucket

**Slot value**:
The smallest hash value that a slot received.
*Avoid*: min-hash, minimum

**Empty marker**:
The slot value of a slot that received no item.
*Avoid*: sentinel

**Test vectors**:
Fixed inputs with their expected outputs, shared by every language implementation.
*Avoid*: pinned values, golden values

### LSH

**Band**:
A fixed group of consecutive slots.
*Avoid*: chunk, segment

**Band key**:
One hash value that summarises one band of one signature.
*Avoid*: band hash, LSH key

**Bucket**:
The set of items that share a band key in one band.
*Avoid*: bin, group

**Candidate pair**:
Two items that share at least one bucket.
*Avoid*: match, hit

**Verified pair**:
A candidate pair whose estimated Jaccard similarity reaches the threshold.
*Avoid*: duplicate pair

**Cluster**:
A set of items that verified pairs connect.
*Avoid*: group, duplicate set

### Delivery

**Issue**:
One Linear work item.
*Avoid*: task

**Phase**:
One stage of delivery, closed by a gate.
*Avoid*: stage, step

**Gate**:
A human-owned issue that closes a phase when its checks pass.
*Avoid*: milestone (a Linear milestone holds the issues of a phase)

**Integration branch**:
The fork branch that collects reviewed MinHash work before any upstream PR.
*Avoid*: dev branch, main

**Port**:
To move reviewed fork code onto the integration branch.
*Avoid*: re-split, rebase (rebase is the git operation)

**Data bridge**:
The temporary path that carries featurizer output from Python to the C++ demos until the Python bindings exist.
*Avoid*: pipeline, export

**Demo**:
The runnable demonstration of a phase on trajectory data, with recorded results.
*Avoid*: example, demonstration

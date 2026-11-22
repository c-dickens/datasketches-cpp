# MinHash demos (fork-only)

This directory holds the demos for the MinHash sketch and the LSH index. It is
fork-only: upstream PRs leave `scripts/` out. The code here may use C++17. It
builds against the in-tree library, and it does not change the root
`CMakeLists.txt`.

## Run every demo

```bash
./run_demos.sh
```

This one command downloads the data (the first time only), builds the demos and
runs them. Later tickets add their demos to it. Set `JOBS` to change the number
of build jobs (default 4).

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The unit tests use small text fixtures. They do not download anything.

## Data

### 20 Newsgroups

```bash
data/get_20news.sh
```

The script downloads the original 20 Newsgroups data (19,997 posts, 20
newsgroups) into `data/`, and extracts it to `data/20_newsgroups/`. Git ignores
the downloaded files. This version keeps the cross-posted duplicates, which the
LSH demo needs.

- Source: the [UCI Machine Learning Repository](https://archive.ics.uci.edu/dataset/113/twenty+newsgroups),
  over HTTPS. The script checks the SHA-256 of the zip and of the
  `20_newsgroups.tar.gz` inside it.
- The same version is also at
  `http://qwone.com/~jason/20Newsgroups/20news-19997.tar.gz`. That host has no
  HTTPS, so cloud sessions cannot reach it. To use it, run
  `NEWS20_URL=http://qwone.com/~jason/20Newsgroups/20news-19997.tar.gz data/get_20news.sh`.

The loader (`include/news20/loader.hpp`) does this for each post:

1. It drops the header block: everything before the first blank line.
2. It converts ASCII letters to lower case.
3. It splits the text on every byte that is not an ASCII letter or digit.
4. It makes the 5-word shingles, each joined by single spaces. The shingles of
   a post are a set. A post with fewer than 5 words has no shingles.

`news20::jaccard` gives the exact Jaccard similarity of two shingle sets.

`build/news20_stats data/20_newsgroups` prints the post count and the total
shingle count. The total shingle count is the sum, over all posts, of the
number of distinct shingles in each post.

### Agent trajectories

```bash
data/get_trajectories.sh                   # 100 task instances
data/get_trajectories.sh --instances 1000  # the LSH demo uses 1,000
```

The script writes `data/trajectories_<N>.jsonl`. It downloads these files into
`data/` the first time, and git ignores them:

- `trajectories.parquet`, about 2 GB, from
  [nebius/SWE-rebench-openhands-trajectories](https://huggingface.co/datasets/nebius/SWE-rebench-openhands-trajectories)
  at a pinned revision. The script checks its SHA-256. The dataset has 67,074
  trajectories of 6,306 task instances. The rows are shuffled, so any subset
  needs the full file.
- the DuckDB CLI v1.5.6 for the platform, from the DuckDB GitHub releases. The
  script checks the SHA-256 of the zip.

**Why DuckDB.** The dataset is one 2 GB parquet file, and C++ needs Apache
Arrow to read parquet. The DuckDB command-line tool is one binary that converts
the file once into JSON lines, which the C++ demos then read. Nothing else uses
DuckDB.

The script chooses the first N task instances, ordered by `md5(instance_id)`,
that have at least 1 resolved and at least 1 unresolved trajectory. If fewer
than N task instances qualify, it stops with an error. It writes one JSON
object per trajectory of those task instances:

- `trajectory_id`, `instance_id`, `resolved` (1 or 0, as in the source) and
  `exit_status`;
- `steps`: the messages from the 3rd message on, each as `{role, fn, args, out}`:
  - `fn` is the tool name: the first tool call's function for an assistant
    message, the `name` of a tool message, and `null` otherwise;
  - `args` is the first tool call's arguments, as the JSON string of the source,
    or `null`;
  - `out` is the message content, cut to 1,000 characters.

## MinHash correctness on 20 Newsgroups

```bash
build/minhash_20news data/20_newsgroups results/minhash_20news.md
```

`./run_demos.sh` runs this demo. It checks the sketch's Jaccard estimates
against the exact values, and that merging is exact. It writes
`results/minhash_20news.md`, which is committed, with each criterion marked
PASS or FAIL and the last line `Overall: PASS` or `Overall: FAIL`. The file
holds no time, date or host name, so a second run gives the same file.

**Pairs.** All pairs of a fixed sample of 2,000 posts (about 2M pairs), plus
all pairs of posts that share a `Message-ID` header (cross-posts). The sample
is the 2,000 post indices with the smallest `XXHash64` of the index, seed 1.

**Accuracy.** For k = 64, 128, 256 and 512, each pair gets its exact J and its
estimate. The pairs go into J bins of width 0.1. Pairs with exact J = 0 or
J = 1 are left out, because the estimate has no variance there. Each bin with
at least 1,000 pairs is checked:

- the mean bias of (estimate − exact) is at most 0.01;
- the observed SD of (estimate − exact) is within ±15% of
  √(mean of J(1−J) over the bin / k);
- `jaccard_bounds()` at 2 and 3 SD covers the exact J at least as often as
  95.45% and 99.73% minus 1 point.

**Merge.** Each post's sorted shingle set is split into 4 consecutive pieces.
The merge of the 4 piece sketches must equal the sketch of the whole set, for
every post and every k.

## Band-key test vectors

`vectors/band_keys.py` computes the 15 band-key test vectors of
`minhash/SPEC.md` ("Band keys") with the Python `xxhash` package, which is
independent of the C++ code. It then checks them against the SPEC.md table.
Run it in a virtualenv. Git ignores `.venv/`. Run these commands from the
repo root, not from this directory:

```bash
python3 -m venv scripts/minhash/vectors/.venv
scripts/minhash/vectors/.venv/bin/pip install -r scripts/minhash/vectors/requirements.txt
scripts/minhash/vectors/.venv/bin/python scripts/minhash/vectors/band_keys.py
```

The last line must be `xxhash 3.5.0 - all 15 band keys match SPEC.md`. If a key does not
match, the script exits with status 1.

## Characterisation

The characterisation harness measures the sketch, in the style of
[apache/datasketches-characterization](https://github.com/apache/datasketches-characterization).
It reports only. It has no PASS or FAIL. The statistical tests stay the
correctness gate.

```bash
./run_characterisation.sh         # the full profiles, into results/characterisation/
./run_characterisation.sh smoke   # the smoke profiles, into results/characterisation/smoke/
```

The script builds in `build-characterise/` (Release). It runs each profile with
`build-characterise/minhash_characterise <profile-name>`, and then plots the
results with `uv run plots/<name>.py <results dir>`. All parameters are
constants in the source code. Git ignores the smoke results. The full results
are committed.

| Profile | What it measures | Full run |
|---|---|---|
| `minhash-accuracy-j` | The error of the Jaccard estimate against the true J, for k = 16 to 4096 | 112 s |
| `minhash-accuracy-j-smoke` | The same, with k ≤ 256, 256 trials and a coarse J grid | less than 1 s |
| `minhash-accuracy-n` | The MinHash and theta Jaccard error against the union size n, from 2 to 65536, at J = 0.1, 0.5 and 0.9, for k = 16 to 4096, with the theta memory against MinHash | 36 min |
| `minhash-accuracy-n-smoke` | The same, with k ≤ 256, 256 trials, n up to 4096 and 4 points per octave | 1 s |
| `minhash-accuracy-n-half` | The same as `minhash-accuracy-n`, with theta at half the nominal k (lg_k = log2(k) − 1), so that theta never uses more memory than MinHash. k = 64 to 4096 | 33 min |
| `minhash-accuracy-n-half-smoke` | The same, with k = 64 and 256, 256 trials, n up to 4096 and 4 points per octave | 1 s |
| `minhash-lsh-accuracy` | The LSH candidate rate against the theory 1 − (1 − J^r)^b, and the reported rate (candidate and estimated J ≥ t, at t = 0.5, 0.7 and 0.9), for k = 64, 128 and 256 and every power-of-2 b, at J = 0 to 1 in steps of 0.01 | 14 s |
| `minhash-lsh-accuracy-smoke` | The same, with k = 64 and 128, 256 trials and J in steps of 0.05 | 1 s |
| `minhash-update-speed` | ns per item of `update()` against n = 1 to 2^20, for MinHash and theta, with `u64`, 8-byte and 64-byte string items | about 1.2 h (estimate) |
| `minhash-update-speed-smoke` | The same, with k ≤ 256 and n ≤ 2^14 | about 1 s |
| `minhash-call-speed` | ns per call of construction, merge and `jaccard()` against k, for MinHash and theta | about 30 s (estimate) |
| `minhash-call-speed-smoke` | The same, with k ≤ 256 and 2^10 calls | less than 1 s |
| `minhash-lsh-performance` | The time, memory, recall and precision of finding all pairs with J ≥ 0.5 among N documents: MinHash with the LSH index ((b, r) = (32, 4) and (16, 8), N = 2^8 to 2^18), MinHash `jaccard()` on all pairs (N ≤ 2^14) and theta `jaccard()` on all pairs (N ≤ 2^12), k = 128 | about 14 min (estimate, Intel Xeon 2.3 GHz) |
| `minhash-lsh-performance-smoke` | The same, with N ≤ 2^12 for LSH, 2^11 and 2^10 for brute force, 100 queries for each class and 1 repeat | 10 s (Intel Xeon 2.3 GHz) |

The run time is for an Apple M4 laptop, not counting the build.

`minhash-accuracy-j` writes these files:

- `accuracy_j.tsv`: one row for each (k, J), with the mean bias, the measured
  SD against the theory SD, the quantiles of the absolute and the normalised
  error, and the coverage of `jaccard_bounds()` at 1, 2 and 3 SD. The `#` lines
  give the commit, the compiler, the build flags and the constants.
- `accuracy_j_error.svg`, `accuracy_j_normalised.svg` and
  `accuracy_j_coverage.svg`: the plots.
- `accuracy_j.md`: a summary for each k, with links to the plots.

`minhash-accuracy-n` writes these files:

- `accuracy_n.tsv`: one row for each (k, J, n), with the bias, the measured SD,
  the error quantiles and the bounds coverage of both sketches, and the minimum
  and maximum retained entries and compact bytes of theta.
- `accuracy_n_error_j10.svg`, `accuracy_n_error_j50.svg`,
  `accuracy_n_error_j90.svg`, `accuracy_n_coverage.svg` and
  `accuracy_n_memory.svg`: the plots.
- `accuracy_n.md`: the end of the small-set region and the theta memory ratio
  for each k, with links to the plots.

`minhash-accuracy-n-half` writes the same files, with the stem `accuracy_n_half`
in place of `accuracy_n`. Its TSV has the line `# theta_lg_k=log2(K)-1`.

`minhash-lsh-accuracy` writes these files:

- `lsh_accuracy.tsv`: one row for each (k, b, J), with the candidate rate, the
  theory rate, their z-score, and the reported rate at each threshold t.
- `lsh_scurve_k64.svg`, `lsh_scurve_k128.svg`, `lsh_scurve_k256.svg` and
  `lsh_reported_k128.svg`: the plots.
- `lsh_accuracy.md`: the largest z-score for each k, and a guidance table for
  each (k, b, r, t): the J where the reported rate reaches 0.5, the reported rate
  at J = t − 0.1 and the miss rate at J = t + 0.1.

The speed profiles (MH-18) compare MinHash with theta at the same k. Run
them on a quiet machine, because other work changes the times. Each trial
times only the update loop, and each call result goes into a sink, so that
the compiler keeps the work. The numbers describe one machine, so the files
are named with a tag `<compiler>_<cpu>`, for example
`appleclang_21_0_0_21000101_apple_m4`:

- `update_speed_<tag>.tsv` and `call_speed_<tag>.tsv`: the profile outputs.
  The `#` lines also give the CPU.
- `update_speed_<tag>.svg` and `call_speed_<tag>.svg`: the plots against n
  and against k. MinHash is solid, and theta is dashed.
- `speed_<tag>.md`: the tables of ns per item at the largest n and of ns per
  call.

The LSH performance profile (MH-20) finds all pairs with J ≥ 0.5 among N
documents of 1000 items. The first tenth of the documents are in planted groups
of 2 to 5 near-duplicates with an exact J of 0.6 to 0.95. The others share no
items. So every true pair is known. It times the phases `sketch`, `add`,
`cluster`, `query_planted`, `query_unrelated` and `brute`. A counting allocator
measures the bytes of the index. It writes its time TSV to stdout and its
quality TSV to the file that `CHARACTERISE_QUALITY_OUT` names. Its files use the
same tag as the speed profiles:

- `lsh_performance_time_<tag>.tsv`: the median and minimum time of each
  (method, b, r, N, phase).
- `lsh_performance_quality_<tag>.tsv`: the true, reported and true-positive
  pairs, the recall and precision, the LSH candidate pairs and largest bucket,
  and the bytes per document, for each (method, b, r, N).
- `lsh_performance_time_<tag>.svg`, `lsh_performance_memory_<tag>.svg` and
  `lsh_performance_quality_<tag>.svg`: the plots. The time plot draws the fitted
  c N² of each brute-force method, dashed, up to N = 2^18.
- `lsh_performance_<tag>.md`: the crossover N and the speed-up of LSH over each
  brute-force method, the time of each phase, the ns per query, and the recall
  and precision.

The full speed and LSH performance results are not committed yet. MH-21 runs
them on a quiet machine.

Read the results with these limits:

- At small n, one trial can take less than one tick of `steady_clock` (about
  42 ns on Apple Silicon). The ns per item there is mostly the clock cost, and
  the minimum can be 0. The table uses the largest n, where this has no effect.
- `merge` is not the same operation for the two sketches. MinHash merges B in
  place into a copy of A. Theta builds a new `theta_union`, updates it with A
  and B, and calls `get_result()`, because a user who merges two theta
  sketches does that.

To open a plot notebook interactively:

```bash
uvx marimo edit plots/accuracy_j.py
```

## Credits

- **20 Newsgroups.** Collected by Ken Lang. Mirrored by Jason Rennie at
  <http://qwone.com/~jason/20Newsgroups/>, and by the UCI Machine Learning
  Repository: Mitchell, T. (1997). *Twenty Newsgroups* [Dataset]. UCI Machine
  Learning Repository. <https://doi.org/10.24432/C5C323>.
- **SWE-rebench-OpenHands-Trajectories.** By Nebius, licensed under
  [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/):
  Trofimova, M., Shevtsov, A., Badertdinov, I., Pyaev, K., Karasik, S. and
  Golubev, A. (2025). *OpenHands Trajectories with
  Qwen3-Coder-480B-A35B-Instruct*. Nebius blog.
  <https://huggingface.co/datasets/nebius/SWE-rebench-openhands-trajectories>.

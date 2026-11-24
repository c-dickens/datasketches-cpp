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
ctest --test-dir build --no-tests=error --output-on-failure
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

## LSH correctness on 20 Newsgroups

```bash
build/lsh_20news data/20_newsgroups results/lsh_20news.md
```

`./run_demos.sh` runs this demo. It indexes every post with a non-empty
shingle set at k = 128, with 32 bands of 4 slots and a threshold of 0.5. It
writes `results/lsh_20news.md`, which is committed, with the last line
`Overall: PASS` or `Overall: FAIL`. `Overall: PASS` needs both checks below.
A pair is a candidate when its band keys are equal in at least one band.

**S-curve check (synthetic sets).** `demo::scurve_check` makes 2,000 pairs of
integer sets in each J bin of width 0.1: S1 = [o, o + a + c) and
S2 = [o + a, o + 2a + c), with 2a + c = 1,000, so exact J = c / 1,000. J is
spread across each bin, and each pair has its own offset o, so no item is in 2
pairs and the pairs are independent. Real posts are not used here: pairs of
posts share posts, so they are not independent, and their candidate counts
spread more than the theory allows. In each bin, the candidate count is a sum of independent
Bernoulli(p_i), with p_i = 1 − (1 − J_i⁴)³². A bin passes when the exact
two-sided p-value of its count is at least 10⁻⁷. So a correct index fails the
table of 10 bins by chance with a probability of at most 10⁻⁶. A bin with
fewer than 1,000 pairs fails ("bin not filled").

`demo_test` runs the same check on the real band keys and on 4 broken
versions, and each broken version must fail: 2-slot bands, 4 of the 32 bands
missing, a band key from 3 of its 4 slots, and every slot read as slot 0 (as if
every slot had the same seed). With 2,000 pairs per bin, the smallest change of
the candidate rate that the check sees is about 2 points near J = 0.1 and
about 5 to 6 points between J = 0.3 and 0.5. 1 missing band changes the rate
by about 1 point, so the check does not see it. The band-key test vectors of the
library catch that fault.

**Cross-post check.** Each pair of indexed posts that share a `Message-ID`
must be in one cluster of `get_duplicate_clusters()`.

**Report.** These use the pairs of the MinHash demo (2,000 sampled posts plus
the `Message-ID` pairs). Pairs with a post that has no shingles are left out,
because such a post cannot be indexed.

- Recall and precision of the verified pairs (candidate and estimated
  J ≥ 0.5) against exact J ≥ 0.5.
- False collisions: the candidates among the pairs with exact J = 0.
- The 10 largest buckets (one bucket is one band key in one band).

The speed of LSH against exact all-pairs is in MH-15, not here.

## Full-data LSH check on 20 Newsgroups (MH-14b)

```bash
FULL=1 ./run_demos.sh
build/lsh_20news_full data/20_newsgroups results
build/lsh_20news_full --smoke data/20_newsgroups results/smoke
```

`./run_demos.sh` runs this demo only when `FULL=1` is set. It compares the
sketch and the LSH index with the exact Jaccard of every pair of indexed posts
(19,925 posts, about 198.5 million pairs), at each of 140 grid points:
k ∈ {64, 128, 256, 512, 1024}, r ∈ {2, 4, 8, 16}, b = k / r and
t ∈ {0.3, 0.4, …, 0.9}. A true pair has exact J ≥ t. A reported pair is a
candidate with Ĵ ≥ t. It writes these files, which are committed:

- `results/lsh_20news_full_pairs.tsv`: the pair counts, recall, precision,
  expected counts, p-values and PASS or FAIL of each grid point.
- `results/lsh_20news_full_clusters.tsv`: the LSH clusters against the true
  clusters (the components of the true pairs) at each grid point.
- `results/lsh_20news_full_estimates.tsv`: the error of Ĵ for each k.
- `results/lsh_20news_full.md`: the summary, with the figures and the last
  line `Overall: PASS` or `Overall: FAIL`.
- `results/lsh_20news_full_*.svg`: the 5 figures of the summary, from
  `plots/lsh_20news_full.py`: the pair and cluster precision–recall curves,
  a table of colour of recall and precision, the counts against the theory,
  and the estimate error against k.
- `results/lsh_20news_full_exact_pairs.tsv.gz`: the exact-pair cache (below).

`Overall: PASS` needs 3 checks:

- **Pair check.** At each grid point, the count of reported true pairs and the
  count of reported false pairs are tested against their theory with the
  exact Poisson-binomial p-value, at a family-wise α of 10⁻⁶ over the 280
  tests. The test treats the pairs as independent. Pairs that share a post
  are positively correlated, so the real false-FAIL rate is higher than 10⁻⁶;
  the report gives the smallest p-value, so the margin is visible. P(reported | J) is the joint probability that some band is all-equal
  and at least ⌈t k⌉ slots are equal. It is not the product of the two
  probabilities, because both use the same slots.
- **Prefix assert.** Slot i does not depend on k, so each signature must equal
  the first k slots of the k = 1024 signature.
- **Cross-posts.** Each `Message-ID` pair must be in one true cluster and in
  one LSH cluster, at every grid point.

The program also asserts that the components of the reported pairs equal
`get_duplicate_clusters()` at every grid point.

**Exact-pair cache.** The exact pass takes about 10 minutes. It stores each
pair with J > 0 as (i, j, |A ∩ B|, |A ∪ B|), with i and j in `load_posts`
order, in `data/lsh_20news_full_exact_pairs.tsv`. Its header holds the
SHA-256 of the archive, the shingle width, the indexed post count, an
XXHash64 fingerprint of every shingle set, and the pair count. The program
loads the cache when the header matches, so any change to the shingling
makes it run the exact pass again and write a new cache. It stops if the
file has fewer or more pairs than its header says, or if a loaded pair does
not give `news20::jaccard`. `run_demos.sh` unpacks the committed
`.tsv.gz` into `data/` when the cache is missing, and it compresses the cache
into `results/` again (`gzip -9n`) after the run.

**Smoke run.** `--smoke` uses only the 2,000 posts of the MinHash demo sample,
with its own cache. Write its results to `results/smoke/`, which git ignores,
because the program writes to the directory that you give it. It
takes about 15 seconds.

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
| `minhash-lsh-performance` | The time, memory, recall and precision of finding all pairs with J ≥ 0.5 among N documents: MinHash with the LSH index ((b, r) = (32, 4) and (16, 8), N = 2^8 to 2^18), MinHash `jaccard()` on all pairs (N ≤ 2^14) and theta `jaccard()` on all pairs (N ≤ 2^12), k = 128 | about 8.5 min |
| `minhash-lsh-performance-smoke` | The same, with N ≤ 2^12 for LSH, 2^11 and 2^10 for brute force, 100 queries for each class and 1 repeat | about 6 s |

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
  pairs, the recall and precision, the LSH candidate pairs, band collisions and
  largest bucket, and the bytes per document, for each (method, b, r, N).
  `CandidatePairs` counts each pair that shares at least one bucket once.
  `BandCollisions` counts a pair once for each band that it shares. It is the
  work that the buckets give. For brute force, `CandidatePairs` is
  N (N − 1) / 2.
- `lsh_performance_time_<tag>.svg`, `lsh_performance_memory_<tag>.svg` and
  `lsh_performance_quality_<tag>.svg`: the plots. The time plot draws the fitted
  c N² of each brute-force method, dashed, up to N = 2^18.
- `lsh_performance_<tag>.md`: the crossover N and the speed-up of LSH over each
  brute-force method, the time of each phase, the ns per query, and the recall
  and precision.

The reported pairs of `minhash_lsh` are all the pairs in one cluster. Clusters
are connected components, so a pair whose estimate is below t is reported when
other members link it. The brute-force methods report only the pairs with
`jaccard()` ≥ t. So the `minhash_lsh` recall is not the pair-level rate.

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

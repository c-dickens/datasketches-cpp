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
| `minhash-update-speed` | ns per item of `update()` against n = 1 to 2^20, for MinHash and theta, with `u64`, 8-byte and 64-byte string items | about 1.1 h (estimate) |
| `minhash-update-speed-smoke` | The same, with k ≤ 256 and n ≤ 2^14 | less than 1 s |
| `minhash-call-speed` | ns per call of construction, merge and `jaccard()` against k, for MinHash and theta | about 1 min (estimate) |
| `minhash-call-speed-smoke` | The same, with k ≤ 256 and 2^10 calls | about 1 s |

The run time is for an Apple M4 laptop, not counting the build.

`minhash-accuracy-j` writes these files:

- `accuracy_j.tsv`: one row for each (k, J), with the mean bias, the measured
  SD against the theory SD, the quantiles of the absolute and the normalised
  error, and the coverage of `jaccard_bounds()` at 1, 2 and 3 SD. The `#` lines
  give the commit, the compiler, the build flags and the constants.
- `accuracy_j_error.svg`, `accuracy_j_normalised.svg` and
  `accuracy_j_coverage.svg`: the plots.
- `accuracy_j.md`: a summary for each k, with links to the plots.

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

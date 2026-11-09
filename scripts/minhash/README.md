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

### Trajectory featurizer (v1)

The featurizer (`include/trajectory/featurizer.hpp`) turns each trajectory into
a set of tokens that describes what the agent did. The tokens are the sketch's
input items. It reads the JSON lines with
[nlohmann/json](https://github.com/nlohmann/json) v3.11.3, which CMake fetches.

1. It normalises each bash call: it removes a leading `cd /workspace/<dir> && `
   from the command. `strip_bash_footer` drops the footer lines
   `[The command completed with exit code N.]`, `[Current working directory: …]`,
   `[Python interpreter: …]` and `[Command finished with exit code N]`.
2. Each step (an assistant message with a tool call) becomes one symbol,
   `tool:subcommand:outcome`:

   | Tool | Subcommand | Outcome |
   |---|---|---|
   | `execute_bash` | the first word of the normalised command | `ok` for exit code 0, `exit<N>` for another code, `timeout` for the timeout notice, `unknown` if the reply has neither |
   | `str_replace_editor` | the `command` argument | `error` if the reply starts with `ERROR:`, else `ok` |
   | `task_tracker` | the `command` argument | `ok` |
   | `think`, `finish` | `-` | `ok` |

   A `user` message that reports a malformed call (it contains
   `[Error occurred in processing last action]`, or says `Tool … is not
   registered.`) sets the outcome of the step before it to `malformed`.
3. A token is 3 steps in a row. Its encoding writes each symbol as
   `<length>:<symbol>` and joins them, so two different symbol lists never give
   the same string.
4. The tokens of a trajectory are a set: repeated tokens count once.

The source cuts each reply to 1,000 characters, and the exit code is in the
footer at the end of the reply. So the outcome of about 42% of bash steps is
`unknown`.

`build/trajectory_stats data/trajectories_100.jsonl` prints the trajectory
count, and the minimum, median and maximum token-set size.

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

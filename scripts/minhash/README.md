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

## Credits

- **20 Newsgroups.** Collected by Ken Lang. Mirrored by Jason Rennie at
  <http://qwone.com/~jason/20Newsgroups/>, and by the UCI Machine Learning
  Repository: Mitchell, T. (1997). *Twenty Newsgroups* [Dataset]. UCI Machine
  Learning Repository. <https://doi.org/10.24432/C5C323>.

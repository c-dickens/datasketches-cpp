<!--
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
-->

# Deduplication of agent trajectories with MinHash: findings

Sample: the MH-15 subset (1,000 task instances, 11,166 trajectories, 579 repos), from
`get_trajectories.sh --instances 1000` and the pinned parquet file. All J values are exact
Jaccard on **all 62,334,195 pairs** (no sampling). The tables come from
`uv run scripts/minhash/analysis/trajectory_dedup.py`, which writes
`scripts/minhash/results/trajectory_dedup.md` (about 2 minutes on a laptop).

## Summary

1. **What clusters is the issue.** Attempts at one issue share a large exact core: the same
   files, the same greps, the same view ranges and often the same edit. The tail of each
   attempt is its own: scratch scripts, checks and the details of the fix. Verbatim or
   near-verbatim duplicates do not exist.
2. **Why MH-E1 did not show it:** about 60% of the words of each trajectory (median 301 of
   483) are words that more than 20% of all trajectories use (`the`, `python`, `test`, `py`,
   `view`, `import`, …). They give every pair J ≈ 0.27, and so the same-repo pairs of other
   tasks sit just under the same-task pairs.
3. **Fix 1 (the owner's item with a DF band):** `action_text_words_w1` without the words in
   more than 20% of trajectories (a stop list of 589 words) and without the words that occur
   in one trajectory only (they cannot match, so they only make unions larger). Same task 0.32,
   same repo 0.096, other repo 0.032. With MinHash at **t = 0.30**, its components recover the
   997 issues with ARI 0.93 to 0.94 at k = 128 and 0.94 to 0.95 at k = 256 (3 seeds each).
4. **Fix 2 (the high-J item):** `paths_edits`: the repo files that the agent touches, plus the
   lines of its `str_replace` edits, with the same DF band. Same-task median J **0.50**; 51% of
   same-task pairs at J ≥ 0.5 and 26% at J ≥ 0.7; same repo 0.028; other repo 0.001. ARI 0.91
   to 0.92 at t = 0.30 for k = 128 and k = 256. Only 7.5% of all pairs have J > 0.
5. **For MH-15b:** at its t = 0.40, `action_text_words_w1` joins 1,550 trajectories of 56 issues
   into one component on exact J (ARI 0.10). This item needs t = 0.45, and the useful window is
   narrow (ARI 0.10 at 0.40, 0.92 at 0.45, 0.85 at 0.50).

## 1. What the traces show

### Same task, resolved attempts: pints-team__pints-1154 (32 attempts, 31 resolved)

The issue: `curve_fit` returns only `xbest`, but its docstring says `(xbest, fbest)`. Four
attempts, normalised (`/workspace/<repo>` is `.`, `python -c` bodies and file contents cut):

| step | b9f92c7c | 562bcf11 | b2e3fa54 | 31cd714d |
|---|---|---|---|---|
| 1 | think | think | think | think |
| 2–4 | view /workspace, ., README.md | view /workspace, ., README.md | view /workspace, README.md | view /workspace, ., README.md |
| 5–6 | `python run-tests.py --help`, `--unit` | same | same | same |
| 7 | `find . -name "*.py" -exec grep -l "curve_fit" {} \;` | same | same | same |
| 8 | `grep -n "def curve_fit" pints/*.py …` | view `pints/_optimisers/__init__.py` | `grep -rn "def curve_fit" …` | `grep -n "def curve_fit" pints/_optimisers/__init__.py` |
| 9–10 | view `_optimisers/__init__.py [840, 900]`, `[900, 960]` | same | same | same |
| … | view `pints/tests/test_opt_easy_optimisation.py` | same | same | same |
| … | create `test_curve_fit_issue.py`, run it | create `reproduce_issue.py`, run it | create `reproduce_issue.py`, run it | create `reproduce_issue.py`, run it |
| edit | `return popt` → `return popt, fopt` | same | `return (popt, fopt)` | same as first |
| tail | `comprehensive_test.py`, pytest `test_curve_fit`, `test_fmin`, 2 × `python -c` | `comprehensive_test.py`, git status, pytest, grep, `final_verification.py`, git diff | `comprehensive_test.py`, pytest, `test_fmin_still_works.py`, `final_verification.py`, task_tracker | `comprehensive_test.py`, `test_assignment.py`, git log ×2, `verify_fix.py`, 3 more scripts, pytest |

The owner's view is right: these are the same attempt. The view ranges `[840, 900]` and
`[900, 960]` are identical in all four. What differs:

- **Noise:** the names and contents of scratch scripts (`test_curve_fit_issue.py` against
  `reproduce_issue.py`), the bodies of `python -c` checks, the wording of think and finish
  steps, git status and git log calls, small grep variants (`grep -n` against `grep -rn`).
- **Shared core:** the repo files (`pints/_optimisers/__init__.py`, `pints/__init__.py`,
  `pints/tests/test_opt_easy_optimisation.py`, `run-tests.py`), the search term `curve_fit`,
  the test ids, and the edit.

With normalised action lines (one item per step), attempts 1 and 2 share 15 lines and differ
in 16 (J 0.48). On the files and edit lines (`paths_edits`), 3 pairs of this task have
J = 1.0 (the same 4 files and the same 6 edit lines), and the median J of the task is 0.69.

### Same task, hard and mostly unresolved: encode__starlette-1715 (24, 2 resolved) and spacetelescope__jwst-3629 (33, 2 resolved)

The opening is the same again: README, `scripts/test`, `starlette/middleware/base.py`,
`starlette/background.py`, `responses.py [1, 100]` and `[100, 200]`, then `test_base.py` and
`test_background.py`. Then the attempts spend 50 to 150 steps on scratch servers (`uvicorn`,
`curl`, `sleep`, `pkill`) with different file names, and they edit different lines of
`base.py`. For jwst, the same files come back in every attempt (`main.py`, `generate.py`,
`lib/rules_level2_base.py`, `tests/test_level2_background.py`, `pool_009_spec_miri_lv2bkg.csv`),
but the view ranges drift (`[525, 600]` against `[530, 600]`), and most edits go to scratch
scripts. Same-task J on `paths` is 0.74 (starlette) and 0.58 (jwst); on the edit lines it is
0.20 and 0.13, because the fixes differ.

### Inside a task, J is narrow for words and two-peaked for edits

| task | words_w1 mean (p10–p90) | paths mean (p10–p90) | edits mean (p10–p90) |
|---|---|---|---|
| pints-1154 | 0.49 (0.46–0.54) | 0.67 (0.50–0.88) | 0.76 (0.16–1.00) |
| starlette-1715 | 0.49 (0.46–0.52) | 0.74 (0.63–0.86) | 0.20 (0.04–0.40) |
| jwst-3629 | 0.46 (0.41–0.50) | 0.58 (0.46–0.71) | 0.13 (0.00–0.39) |
| faker-1711 | 0.52 (0.48–0.56) | 0.58 (0.43–0.73) | 0.43 (0.16–0.72) |

The word set says "same task" with almost the same J for every pair. It does not see whether
two attempts did the same thing. The edit lines do: J is near 1 for the same fix and near 0
for another fix.

### True near-duplicates

Pairs at exact J ≥ 0.9: `words_w1` 0, `lines` 0, `args` 9, `paths_edits` (DF band) 2,831
same-task pairs. So no two trajectories are copies. Near-duplicates exist only in a reduced
representation: the same files and the same edit. The final patches differ even more: only
9.6% of same-task pairs reach patch J ≥ 0.8, because the agents also change docstrings,
comments and tests in their own way.

### Cross-issue merges: what they are

At t = 0.25 on exact J, the word item with the DF band merges 21 groups of issues. Two kinds:

- **Real duplicates across task instances.** `borgbackup__borg-6527`, `-6528` and `-6529` have
  the same issue text (one fix on three branches), and so do `tornadoweb__tornado-2581` and
  `-2589`. The clusters join them without labels. For this reason the cluster tables use the
  issue text as truth (997 issues), not the instance ID.
- **Related issues of one repo.** For example `pypa__pip-12877` (recursive requirements files
  are not detected) and `pypa__pip-13047` (pip reports a recursion that is not there);
  `omni-us__jsonargparse-527` and `-625` (lists of nested dataclasses); `tobymao__sqlglot-3552`
  and `-3392` (Postgres type keywords). The agent opens the same code and edits the same block.
  In `paths_edits`, the sqlglot pair has J 0.92, because each `str_replace` copies the whole
  `KEYWORDS` dict as context: 97 of about 100 items are shared, and the real changes
  (`"FLOAT": TokenType.DOUBLE` against `"INT8": TokenType.BIGINT`) are 3 items. Only one pair
  crosses repos (`SmileyChris__django-countries-339` and `mirumee__django-prices-62`: both
  are Django model fields).

## 2. What a duplicate trajectory is

Two levels are in the data, and they need different items:

- **Issue level (recommended for "keep one per cluster"):** two trajectories are duplicates
  when they attempt the same issue. The behaviour shows it: same files, same searches, same
  tests, the vocabulary of the issue in the agent's own scripts. A cluster is an issue. This
  is the structure the owner sees, and it has a clear gap to other issues after the DF band.
- **Solution level:** two trajectories are duplicates when they also make the same change. At
  this level most attempts are not duplicates (median 9 different final patches for a median
  of 11 attempts). Only edit-line items see it (section 5 of the tables).

The instance ID is a near-perfect label for the issue level, so on this dataset LSH does not
find anything that the labels do not give. The value of MinHash deduplication is where the
labels are missing or wrong: across datasets, across task instances with the same issue
(borg, tornado), and inside an issue at the solution level.

## 3. Candidates (exact J, all pairs)

`_df20`: items in more than 20% of trajectories are dropped. `_min2`: items in fewer than 2
trajectories are dropped. Full table: section 2 of `results/trajectory_dedup.md`.

| Item | size p50 | same task mean / median | same task ≥ 0.3 / 0.5 / 0.7 | same repo mean | other repo mean | t |
|---|---|---|---|---|---|---|
| words_w1 (MH-E1 `action_text_words_w1`) | 483 | 0.492 / 0.494 | 0.999 / 0.444 / 0.000 | 0.324 | 0.268 | 0.40 |
| words_w1_df50 | 286 | 0.349 / 0.349 | 0.830 / 0.005 / 0.000 | 0.141 | 0.086 | 0.25 |
| words_w1_df20 | 181 | 0.320 / 0.317 | 0.616 / 0.007 / 0.000 | 0.095 | 0.032 | 0.20 |
| **words_w1_df20_min2** | 179 | 0.324 / 0.321 | 0.642 / 0.009 / 0.000 | 0.096 | 0.032 | 0.20 |
| words_w1_df2_min2 | 55 | 0.348 / 0.341 | 0.664 / 0.084 / 0.003 | 0.042 | 0.003 | 0.20 |
| nocode_words | 123 | 0.504 / 0.503 | 0.950 / 0.510 / 0.069 | 0.235 | 0.169 | 0.35 |
| lines (one item per normalised step) | 33 | 0.285 / 0.273 | 0.394 / 0.034 / 0.001 | 0.111 | 0.084 | 0.20 |
| opening (first 20 lines) | 17 | 0.330 / 0.310 | 0.540 / 0.116 / 0.012 | 0.146 | 0.084 | 0.20 |
| paths | 11 | 0.609 / 0.600 | 0.982 / 0.774 / 0.290 | 0.259 | 0.099 | 0.40 |
| args (command arguments and paths) | 35 | 0.380 / 0.368 | 0.764 / 0.143 / 0.008 | 0.135 | 0.072 | 0.25 |
| edits_min2 | 38 | 0.487 / 0.493 | 0.676 / 0.498 / 0.303 | 0.005 | 0.000 | 0.20 |
| paths_edits | 64 | 0.390 / 0.354 | 0.577 / 0.328 / 0.138 | 0.039 | 0.016 | 0.20 |
| paths_edits_min2 | 50 | 0.506 / 0.519 | 0.751 / 0.527 / 0.269 | 0.053 | 0.021 | 0.20 |
| **paths_edits_df20_min2** | 47 | 0.491 / 0.500 | 0.726 / 0.507 / 0.257 | 0.028 | 0.001 | 0.20 |
| core_df20_min2 (args + paths + edits) | 62 | 0.432 / 0.434 | 0.716 / 0.392 / 0.100 | 0.034 | 0.002 | 0.20 |

What each rule does:

- **DF > 20% cut:** it attacks the 0.27 background directly (other repo 0.268 → 0.032). It
  costs same-task J (0.49 → 0.32), because the common words were in every intersection. A
  stop list counted on half the task instances gives the same clusters (section 5b), so a
  fixed list of 586 words, stored once, can replace the corpus pass.
- **DF < 2 cut:** it does not change any intersection; it only removes items that no other
  trajectory has. It raises J most where sets are small: `paths_edits` 0.39 → 0.51,
  `edits` 0.37 → 0.49. It needs a pass over the corpus that is deduplicated (batch, not
  streaming).
- **Removing think and finish text** makes clusters worse: `nocode_words_df20_min2` ARI 0.897,
  and a scratch test with the words of the bash and editor arguments only (not in the tables)
  0.93, against 0.972. The agent's notes also carry the vocabulary of the issue.
- **Steps as lines or as a sequence opening** stay low (0.28 to 0.36): one changed flag or file
  name breaks a line item.
- **Context lines in edits:** keeping only the changed lines of each `str_replace`
  (`diffedits_min2`) gives the best solution-level item (same patch 0.88, other patch 0.15,
  AUC 0.95) but worse issue clusters (ARI 0.867; 534 trajectories have no edit of a repo file). The context lines name the code region, so they help at
  the issue level and blur the fix at the solution level.

## 4. Cluster quality

Connected components of the pairs with J ≥ t, against the 997 issues. Kept = components =
trajectories that deduplication keeps (one per component).

### On exact J (the best t of each item)

| Item | t | kept | ARI | purity | merged | split | singletons | largest |
|---|---|---|---|---|---|---|---|---|
| words_w1 | 0.45 | 1,185 | 0.916 | 0.974 | 17 | 131 | 200 | 128 |
| words_w1 at MH-15b's t | 0.40 | 791 | 0.096 | 0.780 | 56 | 17 | 17 | 1,550 |
| words_w1_df20_min2 | 0.25 | 1,031 | 0.972 | 0.980 | 21 | 48 | 57 | 48 |
| words_w1_df20_min2 | 0.30 | 1,693 | 0.938 | 0.996 | 6 | 314 | 633 | 48 |
| paths_edits_df20_min2 | 0.30 | 1,406 | 0.906 | 0.973 | 41 | 337 | 214 | 48 |
| paths_edits_df20_min2 | 0.50 | 2,668 | 0.788 | 0.996 | 12 | 673 | 1,048 | 48 |
| core_df20_min2 | 0.30 | 1,364 | 0.932 | 0.985 | 24 | 288 | 207 | 48 |
| paths | 0.75 | 5,510 | 0.563 | 0.963 | 45 | 864 | 4,423 | 58 |

The largest component (48) is the borg issue: three task instances with one issue text.

### On MinHash estimates (section 5 of the tables)

Real sketches: k random permutations of the items (numpy `default_rng`, seeds 1 to 3), for all
pairs with exact J ≥ 0.10; the binomial bound for the pairs below 0.10 is 0 at t ≥ 0.25.

| Item | k | t | kept (3 seeds) | ARI (3 seeds) | purity | merged | largest |
|---|---|---|---|---|---|---|---|
| words_w1_df20_min2 | 128 | 0.25 | 955–1,001 | 0.60–0.86 | 0.92–0.94 | 32–38 | 116–396 |
| words_w1_df20_min2 | 128 | **0.30** | 1,520–1,673 | **0.93–0.94** | 0.98–0.99 | 13–19 | 48–52 |
| words_w1_df20_min2 | 256 | 0.25 | 997–1,006 | 0.91–0.94 | 0.96 | 30–32 | 57–110 |
| words_w1_df20_min2 | 256 | **0.30** | 1,529–1,618 | **0.94–0.95** | 0.99 | 7–11 | 48 |
| paths_edits_df20_min2 | 128 | 0.25 | 1,188–1,205 | 0.82–0.86 | 0.95 | 49–54 | 123–181 |
| paths_edits_df20_min2 | 128 | **0.30** | 1,381–1,408 | **0.91–0.92** | 0.97–0.98 | 38–46 | 48 |
| paths_edits_df20_min2 | 256 | **0.30** | 1,383–1,393 | **0.91** | 0.97 | 42–44 | 48 |
| paths_edits_df20_min2 | 128 | 0.35 | 1,593–1,602 | 0.90 | 0.99 | 30–34 | 48 |

Two facts:

- The word item with the DF band is **not stable at its exact-J optimum t = 0.25 with k = 128**:
  estimate noise pushes same-repo pairs at J 0.18 to 0.24 over t, and chains grow (ARI 0.60 to
  0.86 over 3 seeds, a component of up to 396). At t = 0.30 it is stable at both k. Use
  **t = 0.30**.
- `paths_edits_df20_min2` changes little between exact J, k = 128 and k = 256, because the pairs
  of other issues sit near J = 0 and not just under t. This is the clear gap.

## 5. What deduplication keeps

Item 1 (`words_w1_df20_min2`) at t = 0.30 on exact J (section 7 of the tables):

- 1,693 components for 997 issues: 1,060 components of 2 or more trajectories, and 633
  singletons. A singleton is an attempt that went its own way; they are mostly unresolved
  (at t = 0.25, 33% of singletons are resolved against 56% of all trajectories).
- 1,249 components (74%) have a resolved trajectory. A rule "keep a resolved one where there
  is one" keeps 1,249 resolved trajectories.
- 6 merged components, all of related issues of one repo: `NREL__hescore-hpxml-103` + `-163`,
  `wemake-python-styleguide-1766` + `-2500`, `sqlglot-784` + `-884`, `sqlglot-3392` + `-3552`,
  `pip-12877` + `-13047`, `sqllineage-492` + `-494`. The highest J between two issues is 0.43.
- The borg component (48 trajectories, 3 task instances, 1 issue) is the largest.

Item 2 (`paths_edits_df20_min2`) keeps 1,406 at t = 0.30 (41 merged components, 214 singletons)
and 2,668 at t = 0.50 (12 merged, purity 0.996).

So issue-level deduplication keeps 10% to 15% of the trajectories (1,031 to 1,693, by t and
item), and solution-level deduplication (t = 0.50 on item 2) keeps 24%.

## 6. Recommendation

- **Item 1, issue-level deduplication: `words_w1_df20_min2`.** It is the owner's item
  (`action_text_words_w1`, the same word tokenizer as 20 Newsgroups and Reuters) plus two
  corpus rules: a stop list of the words in more than 20% of trajectories, and no words that
  occur in one trajectory only. t = 0.30, k = 128 (k = 256 for more margin). Exact J: ARI 0.938,
  purity 0.996.
- **Item 2, the high-J item: `paths_edits_df20_min2`.** Repo files touched (editor paths and path
  arguments of bash commands, scratch files left out) plus the trimmed lines of `str_replace`
  edits on repo files, `o:` / `n:` prefixed. It is the only candidate that gives same-task
  J ≥ 0.5 for half of the pairs with a near-zero background, it holds under MinHash noise,
  and it also separates same-fix from other-fix attempts (AUC 0.85). t = 0.30 for issue
  clusters (keeps 1,406), or t = 0.50 for a stricter "same files and same edit" duplicate
  (keeps 2,668, purity 0.996).
- **Cost of the featurisation:** item 1 needs one DF count (or the stored stop list) and one
  pass for the DF < 2 rule. Item 2 parses the tool-call JSON (`path`, `old_str`, `new_str`,
  and `shlex` tokens of bash commands), which is light, but it is specific to the OpenHands
  tool schema (`str_replace_editor`, `execute_bash`).

## 7. What changes for MH-15b (CD-416)

The owner decides. The facts:

- With `action_text_words_w1` at t = 0.40, the cluster check of MH-15b will show one component
  of about 1,550 trajectories on exact J. t = 0.45 is the best on exact J, with a narrow window.
- With item 1 at t = 0.30, the J > 0 share stays at 99.9% of pairs (the exact-pair cache problem
  of CD-416 Q1 does not change).
- With item 2, only 4.66 million pairs (7.5%) have J > 0, so the exact-pair cache is small and
  the full run is shorter.
- The truth for the cluster check can be the issue text, not the instance ID (it differs for
  5 task instances).

## Method notes

- Exact J for all pairs: binary item matrix; columns in more than 1% of trajectories through
  dense float32 BLAS, the others through sparse products; blocks of 512 rows. Integer counts
  in float32 are exact below 2^24.
- Deterministic: no sampling for J. The MinHash check uses numpy `default_rng(seed)`
  permutations over a sorted vocabulary, so the output does not depend on `PYTHONHASHSEED`
  (two runs with different seeds give the same file).
- Not done: weighted Jaccard (MH-E1 found no gain), the LSH banding (MH-15b), the full 67,074
  trajectories, and an owner check of the merged clusters.

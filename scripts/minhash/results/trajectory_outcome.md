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

# Trajectory outcome: trace signals against patch agreement

Written by `uv run scripts/minhash/analysis/trajectory_outcome.py`. The discussion is in section 8 of `scripts/minhash/analysis/trajectory_dedup_findings.md`.

Sample: 1000 task instances, 11166 trajectories (6242 resolved). Every task instance has both outcomes, so each comparison below is between attempts at the same task. Within-task AUC: P(resolved attempt scores higher than an unresolved attempt of the same task); 0.5 is chance.

## 1. Trace signals

| Signal | resolved mean | unresolved mean | within-task AUC |
|---|---|---|---|
| last pytest run passes | 0.880 | 0.852 | 0.508 |
| share of pytest runs that fail | 0.228 | 0.242 | 0.478 |
| pytest run after the last edit | 0.884 | 0.776 | 0.556 |
| hit the iteration cap | 0.073 | 0.199 | 0.451 |
| called finish | 0.925 | 0.784 | 0.563 |
| finish message admits a problem | 0.071 | 0.077 | 0.497 |
| edits (str_replace, insert) | 5.361 | 6.352 | 0.468 |
| failed edits | 0.338 | 0.420 | 0.478 |
| tool calls | 66.987 | 71.743 | 0.448 |

## 2. Agreement of final patches

Patch lines: MH-E1 `patch_no_new` (changed lines of existing files). Patch files: the existing files that the patch changes.

| Pair inside one task | pairs | patch-line J mean | share ≥ 0.8 | patch-file J mean |
|---|---|---|---|---|
| resolved – resolved | 26634 | 0.364 | 0.180 | 0.889 |
| resolved – unresolved | 21794 | 0.131 | 0.007 | 0.725 |
| unresolved – unresolved | 17077 | 0.209 | 0.077 | 0.758 |

Where the fix goes (leave-one-out: the files that the other resolved attempts of the task change):

- Resolved: the patch changes a file that another resolved attempt changes: 0.996 (n 6092; no existing file changed: 4).
- Unresolved: the patch changes a file that another resolved attempt changes: 0.860 (n 4924; no existing file changed: 246).
- Unresolved attempts with exactly the changed lines of a resolved attempt of the same task: 12 of 4924.

## 3. Detecting the outcome from the patch

| Score | uses labels of other attempts | within-task AUC |
|---|---|---|
| Mean patch-line J to the other attempts of the task (consensus) | no | 0.623 |
| Nearest resolved attempt minus nearest unresolved attempt (patch-line J) | yes | 0.777 |
| Fewer changed lines | no | 0.487 |

Pick one attempt per task: a random one is resolved with probability 0.549; the one with the highest consensus 0.600; the one with the lowest 0.390 (ties broken by trajectory id).

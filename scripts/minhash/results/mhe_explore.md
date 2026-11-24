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

# MH-E item analysis

Written by `uv run scripts/minhash/analysis/mhe_explore.py`. The ticket is MH-E1. All pair statistics use exact Jaccard on the full message content of the pinned parquet file. The owner correction of 24 Nov 2026 applies: action text widths 1, 2, 3 and 5, the shares of pairs at J ≥ 0.3 / 0.5 / 0.7, the choice by median related J inside 0.02 AUC of the best, and the runner-up.

## 1. Sample

- Task instances: 1000 (the first 1000 by `md5(instance_id)` with at least 1 resolved and 1 unresolved trajectory). Repos: 579.
- Trajectories: 11166. Resolved share: 0.559.
- Empty patches (left out of the patch items): 11.
- Pairs: related 65505, hard negative 50000, unrelated 50000, RR 26634, RU 21794, UU 17077.

Facts of the ticket, checked on this sample:

- Bash commands that start with `cd /workspace/<repo> &&` or `;`: 0.962 (ticket: 0.97).
- Bash commands whose first word after the prefix is `python` or `python3`: 0.571 (ticket: 0.55 for `python`, 0.022 for `python3`).
- Bash replies with an exit code: 0.9932 (ticket: 0.9963). Replies that contain `[Command finished with exit code` and have it at the end: 388135 of 388135.
- Trajectories that repeat a bash command: 0.982 (ticket: 0.977).
- Median lines of `model_patch`: 162 (ticket: 121). Median changed lines: 115.

## 2. Candidates

AUC HN: related vs hard negative. AUC UN: related vs unrelated. wAUC HN: the same with weighted Jaccard. Repeat: the share of distinct (trajectory, item) pairs whose item occurs more than once in the trajectory. RR vs RU: AUC with its 99% bootstrap interval (2,000 resamples of task instances, `default_rng(1)`).

| Candidate | size min / p10 / p50 / p90 / max | AUC HN | AUC UN | Repeat | wAUC HN | AUC RR vs RU [99% CI] |
|---|---|---|---|---|---|---|
| files | 5 / 10 / 14 / 21 / 48 | 0.904 | 0.990 | 0.240 | 0.964 | 0.569 [0.543, 0.597] |
| commands | 14 / 33 / 45 / 64 / 89 | 0.966 | 0.990 | 0.147 | 0.976 | 0.587 [0.557, 0.616] |
| steps_w1 | 6 / 11 / 14 / 17 / 33 | 0.664 | 0.757 | 0.697 | 0.755 | 0.548 [0.527, 0.571] |
| steps_w2 | 9 / 28 / 36 / 47 / 71 | 0.680 | 0.802 | 0.320 | 0.733 | 0.532 [0.506, 0.556] |
| steps_w3 | 14 / 37 / 51 / 69 / 91 | 0.673 | 0.796 | 0.147 | 0.702 | 0.532 [0.504, 0.559] |
| steps_w4 | 15 / 40 / 57 / 82 / 97 | 0.675 | 0.792 | 0.064 | 0.692 | 0.533 [0.507, 0.560] |
| steps_w5 | 15 / 40 / 59 / 88 / 96 | 0.675 | 0.781 | 0.027 | 0.682 | 0.532 [0.508, 0.556] |
| action_text_w1 | 149 / 707 / 946 / 1294 / 2667 | 0.997 | 1.000 | 0.486 | 0.989 | 0.629 [0.602, 0.658] |
| action_text_w2 | 222 / 1341 / 1904 / 2783.5 / 6143 | 0.998 | 1.000 | 0.308 | 0.997 | 0.628 [0.601, 0.657] |
| action_text_w3 | 237 / 1576 / 2288 / 3416 / 7840 | 0.998 | 1.000 | 0.228 | 0.997 | 0.625 [0.597, 0.653] |
| action_text_w5 | 250 / 1771 / 2607 / 3928 / 9297 | 0.998 | 1.000 | 0.167 | 0.997 | 0.615 [0.587, 0.643] |

| Candidate | J related mean / median | J hard negative mean / median | J unrelated mean / median | J RR mean / median | J RU mean / median | J UU mean / median |
|---|---|---|---|---|---|---|
| files | 0.396 / 0.381 | 0.191 / 0.174 | 0.101 / 0.094 | 0.415 / 0.400 | 0.383 / 0.367 | 0.382 / 0.364 |
| commands | 0.194 / 0.183 | 0.069 / 0.064 | 0.046 / 0.041 | 0.207 / 0.196 | 0.185 / 0.174 | 0.185 / 0.174 |
| steps_w1 | 0.691 / 0.688 | 0.624 / 0.625 | 0.581 / 0.579 | 0.702 / 0.706 | 0.682 / 0.688 | 0.686 / 0.688 |
| steps_w2 | 0.347 / 0.344 | 0.293 / 0.290 | 0.250 / 0.246 | 0.352 / 0.349 | 0.342 / 0.339 | 0.346 / 0.342 |
| steps_w3 | 0.177 / 0.170 | 0.137 / 0.133 | 0.108 / 0.104 | 0.182 / 0.174 | 0.173 / 0.167 | 0.175 / 0.168 |
| steps_w4 | 0.089 / 0.081 | 0.060 / 0.056 | 0.043 / 0.038 | 0.092 / 0.084 | 0.086 / 0.079 | 0.086 / 0.079 |
| steps_w5 | 0.044 / 0.036 | 0.024 / 0.019 | 0.015 / 0.010 | 0.047 / 0.038 | 0.042 / 0.034 | 0.042 / 0.034 |
| action_text_w1 | 0.280 / 0.278 | 0.141 / 0.140 | 0.116 / 0.116 | 0.290 / 0.289 | 0.271 / 0.270 | 0.275 / 0.272 |
| action_text_w2 | 0.139 / 0.136 | 0.041 / 0.040 | 0.030 / 0.029 | 0.146 / 0.143 | 0.132 / 0.129 | 0.136 / 0.132 |
| action_text_w3 | 0.083 / 0.079 | 0.016 / 0.015 | 0.010 / 0.010 | 0.089 / 0.085 | 0.078 / 0.075 | 0.081 / 0.076 |
| action_text_w5 | 0.045 / 0.041 | 0.004 / 0.004 | 0.002 / 0.002 | 0.049 / 0.045 | 0.042 / 0.038 | 0.043 / 0.039 |

Share of pairs with set J at or above each level (deduplication and LSH need pairs above t):

| Candidate | related J ≥ 0.3 | related J ≥ 0.5 | related J ≥ 0.7 | hard negative J ≥ 0.3 | hard negative J ≥ 0.5 | hard negative J ≥ 0.7 |
|---|---|---|---|---|---|---|
| files | 0.747 | 0.233 | 0.027 | 0.129 | 0.008 | 0.000 |
| commands | 0.087 | 0.002 | 0.000 | 0.000 | 0.000 | 0.000 |
| steps_w1 | 1.000 | 0.961 | 0.481 | 0.998 | 0.893 | 0.257 |
| steps_w2 | 0.696 | 0.057 | 0.001 | 0.450 | 0.006 | 0.000 |
| steps_w3 | 0.053 | 0.001 | 0.000 | 0.004 | 0.000 | 0.000 |
| steps_w4 | 0.003 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| steps_w5 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| action_text_w1 | 0.307 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| action_text_w2 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| action_text_w3 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| action_text_w5 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |

Outcome claims (Q14): files: no outcome difference is found with this item; commands: no outcome difference is found with this item; steps_w1: no outcome difference is found with this item; steps_w2: no outcome difference is found with this item; steps_w3: no outcome difference is found with this item; steps_w4: no outcome difference is found with this item; steps_w5: no outcome difference is found with this item; action_text_w1: **an outcome difference is found**; action_text_w2: **an outcome difference is found**; action_text_w3: **an outcome difference is found**; action_text_w5: **an outcome difference is found**.

## 3. Choice

Q8:

1. Rank by AUC (related vs hard negative): action_text_w3 0.998, action_text_w2 0.998, action_text_w5 0.998, action_text_w1 0.997, commands 0.966, files 0.904, steps_w2 0.680, steps_w4 0.675, steps_w5 0.675, steps_w3 0.673, steps_w1 0.664.
2. No candidate is left out for its set size (owner correction of 24 Nov 2026). The set sizes are in the table of section 2.
3. Best AUC 0.998. Within 0.02 of it, with the median related J: action_text_w3 0.079, action_text_w2 0.136, action_text_w5 0.041, action_text_w1 0.278. The highest median related J is **action_text_w1**.
4. Runner-up (the next candidate by the same rule: the near set by median related J, then the others by AUC): **action_text_w2**.
5. The outcome-class AUCs (Q14) are reported in the table, and they do not take part in the choice.

### Chosen: action_text_w1

Q9: Set AUC 0.997, weighted AUC 0.989, difference -0.008. The difference is less than 0.03, so the recommendation is **plain sets**.

Q13:

- Measure: set Jaccard. The J that maximises TPR − FPR is 0.1926 (TPR − FPR = 0.968). Rounded to the nearest 0.05: **t = 0.20**.
- k: √(t(1−t)/64) = 0.0500, √(t(1−t)/128) = 0.0354, √(t(1−t)/256) = 0.0250. The smallest with ≤ 0.05 is **k = 64**.
- (b, r) of the MH-19 table for k = 64, with the S-curve midpoint: (1, 64) 0.989, (2, 32) 0.962, (4, 16) 0.891, (8, 8) 0.733, (16, 4) 0.454, (32, 2) 0.146, (64, 1) 0.011.
- No (b, r) of the table has its midpoint within 0.05 of t.

### Runner-up: action_text_w2

Q9: Set AUC 0.998, weighted AUC 0.997, difference -0.001. The difference is less than 0.03, so the recommendation is **plain sets**.

Q13:

- Measure: set Jaccard. The J that maximises TPR − FPR is 0.0706 (TPR − FPR = 0.983). Rounded to the nearest 0.05: **t = 0.05**.
- **Q13 stop:** t < 0.2, so no (b, r) and no k are given. The owner decides.

## 4. Human check

Item: action_text_w1. The 20 pairs with the highest J, then the 10 pairs nearest to t = 0.20, from all sampled pairs. Items longer than 160 characters are cut, marked with …

### Highest J

<details><summary>J = 0.504 · related · chatcmpl-66eede70d32873ecfe57968c4ec7edf1 (joblib__joblib-985, resolved 1) · chatcmpl-7ab3807cfd4b97231125cc084590fc61 (joblib__joblib-985, resolved 1)</summary>

First 40 items of chatcmpl-66eede70d32873ecfe57968c4ec7edf1:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
problem
involves
`effective_n_jobs()`
function
returning
incorrect
values
when
used
within
a
`parallel_backend`
context.
2.
In
the
example
code:
-
A
`parallel_backend("threading",
n_jobs=4)`
is
set
up
with
4
jobs
-
Inside
```

First 40 items of chatcmpl-7ab3807cfd4b97231125cc084590fc61:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
problem
is
about
`effective_n_jobs`
function
returning
incorrect
values
when
used
within
a
`parallel_backend`
context.
2.
In
the
example
code:
-
They
set
up
a
parallel
backend
with
threading
and
n_jobs=4
```

</details>

<details><summary>J = 0.495 · related · chatcmpl-2f6142fb49e38d68cb188a1a10c2b088 (matchms__matchms-539, resolved 1) · chatcmpl-e9a5f77cfe1ed821386fe6c458641397 (matchms__matchms-539, resolved 1)</summary>

First 40 items of chatcmpl-2f6142fb49e38d68cb188a1a10c2b088:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
bug
is
about
reading
a
spectrum
with
pepmass
information
that
fails
upon
metadata
harmonization
2.
The
PEPMASS
field
contains:
`(981.54,
None)`
-
this
looks
like
a
tuple
representation
as
a
```

First 40 items of chatcmpl-e9a5f77cfe1ed821386fe6c458641397:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
bug
occurs
when
reading
a
spectrum
with
pepmass
information
that
fails
upon
metadata
harmonization
2.
The
PEPMASS
field
contains:
"PEPMASS:
(981.54,
None)"
3.
This
suggests
that
the
pepmass
parsing
logic
doesn't
```

</details>

<details><summary>J = 0.494 · related · chatcmpl-063ec1c5e2e84822c9727b50e8859714 (repobee__repobee-174, resolved 1) · chatcmpl-293b0a7f30943b7d8e7d223ef676ae94 (repobee__repobee-174, resolved 1)</summary>

First 40 items of chatcmpl-063ec1c5e2e84822c9727b50e8859714:

```text
Let
me
break
down
the
issue
description:
1.
The
goal
is
to
remove
type
validation
and
associated
tests
2.
The
reason
is
that
type
validation
"is
only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
```

First 40 items of chatcmpl-293b0a7f30943b7d8e7d223ef676ae94:

```text
Let
me
analyze
the
issue
description:
1.
The
goal
is
to
"Remove
type
validation
and
associated
tests"
2.
The
reason
is
that
it's
"only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
suggest
using
MyPy
```

</details>

<details><summary>J = 0.473 · related · chatcmpl-8e319b4e68a398d4a819198bee259386 (PyPSA__PyPSA-891, resolved 1) · chatcmpl-bc81fc4d36a2c461eb6b3635b52c16bb (PyPSA__PyPSA-891, resolved 1)</summary>

First 40 items of chatcmpl-8e319b4e68a398d4a819198bee259386:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code/config
snippets
analysis:
-
The
code
uses
PyPSA's
spatial
clustering
functions
-
Two
methods
are
mentioned:
`cluster.get_clustering_from_busmap(...)`
and
`cluster.cluster_by_busmap(...)`
-
Investment
periods
are
added
using
`n.set_investment_periods([2020,
2030])`
-
The
example
follows
```

First 40 items of chatcmpl-bc81fc4d36a2c461eb6b3635b52c16bb:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code
snippets
analysis:
-
The
code
uses
PyPSA's
spatial
clustering
functions
-
Two
methods
are
mentioned:
`cluster.get_clustering_from_busmap(...)`
and
`cluster.cluster_by_busmap(...)`
-
Investment
periods
are
added
using
`n.set_investment_periods([2020,
2030])`
-
The
issue
shows
```

</details>

<details><summary>J = 0.471 · related · chatcmpl-0b0fad5b6b332d9097873713e09e783a (florimondmanca__fountain-lang-7, resolved 0) · chatcmpl-19b66d7e399c265d3670c670d0e2f2f9 (florimondmanca__fountain-lang-7, resolved 0)</summary>

First 40 items of chatcmpl-0b0fad5b6b332d9097873713e09e783a:

```text
Let
me
carefully
read
and
understand
the
issue
description:
1.
The
problem
is
that
expression
statements
like
`"hello,
world"`
or
`some_func(a,
b,
c)`
currently
print
their
results
to
the
console
automatically.
2.
This
behavior
is
desired
in
REPL
mode
(`fountain
```

First 40 items of chatcmpl-19b66d7e399c265d3670c670d0e2f2f9:

```text
Let
me
break
down
the
issue
description:
1.
Current
problem:
Expression
statements
like
`"hello,
world"`
or
`some_func(a,
b,
c)`
result
in
automatic
printing
to
console
when
running
from
a
file,
which
is
unwanted
behavior.
2.
Expected
behavior:
-
In
REPL
```

</details>

<details><summary>J = 0.471 · related · chatcmpl-66eede70d32873ecfe57968c4ec7edf1 (joblib__joblib-985, resolved 1) · chatcmpl-e9984231c5151bf3fd0a59e65087c373 (joblib__joblib-985, resolved 1)</summary>

First 40 items of chatcmpl-66eede70d32873ecfe57968c4ec7edf1:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
problem
involves
`effective_n_jobs()`
function
returning
incorrect
values
when
used
within
a
`parallel_backend`
context.
2.
In
the
example
code:
-
A
`parallel_backend("threading",
n_jobs=4)`
is
set
up
with
4
jobs
-
Inside
```

First 40 items of chatcmpl-e9984231c5151bf3fd0a59e65087c373:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
user
is
using
joblib's
`effective_n_jobs`
function
to
determine
the
number
of
jobs
to
use
2.
They're
using
a
`parallel_backend("threading",
n_jobs=4)`
context
manager
3.
When
they
call
`effective_n_jobs(None)`
inside
the
context,
```

</details>

<details><summary>J = 0.467 · related · chatcmpl-29b7760ce3dd084389f0dbf20b1f3595 (PennyLaneAI__pennylane-1357, resolved 0) · chatcmpl-73d2a7b79d2ea4d5eaf477e8656767db (PennyLaneAI__pennylane-1357, resolved 0)</summary>

First 40 items of chatcmpl-29b7760ce3dd084389f0dbf20b1f3595:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code/config
snippets
analysis:
-
The
example
shows
calling
`AmplitudeDamping(2,
wires=0).kraus_matrices`
-
The
parameter
value
is
2,
which
seems
to
be
invalid
for
a
probability
parameter
-
The
expected
behavior
is
to
```

First 40 items of chatcmpl-73d2a7b79d2ea4d5eaf477e8656767db:

```text
view
/workspace
Let
me
analyze
the
issue
description:
1.1
Code/config
snippets
analysis:
-
The
example
shows
`AmplitudeDamping(2,
wires=0)`
being
called
with
parameter
value
2
-
This
produces
a
RuntimeWarning
about
invalid
value
encountered
in
sqrt
-
The
kraus_matrices
method
shows
```

</details>

<details><summary>J = 0.466 · related · chatcmpl-14491a103bb22edae0a4d8f7bf7fdf13 (robotframework__SeleniumLibrary-1312, resolved 0) · chatcmpl-280d3dc5db920766cccb5592ade35ea5 (robotframework__SeleniumLibrary-1312, resolved 0)</summary>

First 40 items of chatcmpl-14491a103bb22edae0a4d8f7bf7fdf13:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
problem
occurs
in
the
"Get
Cookies"
keyword
in
SeleniumLibrary
2.
The
issue
happens
when
Selenium's
get_cookie()
method
returns
a
dictionary
with
MORE
keys
than
expected
3.
Expected
keys
are:
name,
value,
```

First 40 items of chatcmpl-280d3dc5db920766cccb5592ade35ea5:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
problem
occurs
with
the
"Get
Cookies"
keyword
in
SeleniumLibrary
2.
The
Selenium
`get_cookie`
method
returns
a
dictionary
with
cookie
information
3.
The
current
implementation
hardcodes
specific
keys
that
are
expected:
```

</details>

<details><summary>J = 0.465 · related · chatcmpl-19ecc9d5336da9721a324629cae8bad8 (widdowquinn__pyani-207, resolved 1) · chatcmpl-72fcdaee7a41e830b9067fa4f490f746 (widdowquinn__pyani-207, resolved 1)</summary>

First 40 items of chatcmpl-19ecc9d5336da9721a324629cae8bad8:

```text
Let
me
analyze
the
issue
description:
1.
Error
message:
"TypeError:
subcmd_index()
missing
1
required
positional
argument:
'logger'"
2.
Command
causing
the
error:
"pyani
index
indir
../../genomes/"
3.
Context:
pyANI
version
0.3.0-alpha,
Python
3.8
4.
Expected
behavior:
Should
create
md5
index
```

First 40 items of chatcmpl-72fcdaee7a41e830b9067fa4f490f746:

```text
Let
me
analyze
the
issue
description:
1.
Error
message:
"TypeError:
subcmd_index()
missing
1
required
positional
argument:
'logger'"
2.
Command
that
fails:
"pyani
index
indir
../../genomes/"
3.
Context:
pyANI
version
0.3.0-alpha,
Python
3.8
4.
Expected
behavior:
Should
create
md5
index
files
```

</details>

<details><summary>J = 0.465 · related · chatcmpl-13062dbeb11eba6fa33b5ca890648597 (bridgecrewio__checkov-2214, resolved 0) · chatcmpl-32f38ae56867c0eaf7f9b11c97fa2198 (bridgecrewio__checkov-2214, resolved 0)</summary>

First 40 items of chatcmpl-13062dbeb11eba6fa33b5ca890648597:

```text
Let
me
break
down
the
issue:
1.1
Code/config
snippet
analysis:
-
The
Terraform
resource
`azurerm_app_service`
has
a
`site_config`
block
-
Within
that,
`dotnet_framework_version
=
"v6.0"`
is
set
-
This
should
NOT
trigger
a
warning
for
CKV_AZURE_80
1.2
Technical
details:
-
```

First 40 items of chatcmpl-32f38ae56867c0eaf7f9b11c97fa2198:

```text
Let
me
break
down
the
issue:
1.1
Code/config
snippets
analysis:
-
The
Terraform
resource
uses
`azurerm_app_service`
-
The
configuration
sets
`dotnet_framework_version
=
"v6.0"`
in
the
`site_config`
block
-
This
follows
standard
Terraform
Azure
provider
syntax
1.2
Technical
details:
-
Rule
```

</details>

<details><summary>J = 0.465 · related · chatcmpl-293b0a7f30943b7d8e7d223ef676ae94 (repobee__repobee-174, resolved 1) · chatcmpl-fe92a4e08e0af399f01e19ea7d772236 (repobee__repobee-174, resolved 1)</summary>

First 40 items of chatcmpl-293b0a7f30943b7d8e7d223ef676ae94:

```text
Let
me
analyze
the
issue
description:
1.
The
goal
is
to
"Remove
type
validation
and
associated
tests"
2.
The
reason
is
that
it's
"only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
suggest
using
MyPy
```

First 40 items of chatcmpl-fe92a4e08e0af399f01e19ea7d772236:

```text
Let
me
analyze
the
issue
description:
1.
The
goal
is
to
remove
type
validation
and
associated
tests
2.
The
reason
is
that
it's
"only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
recommend
using
MyPy
```

</details>

<details><summary>J = 0.463 · related · chatcmpl-0d9d5d925018fa663bef3cedaa9b8331 (facelessuser__pymdown-extensions-1043, resolved 1) · chatcmpl-59aafb09bb7679fb7733238b9fe6c536 (facelessuser__pymdown-extensions-1043, resolved 1)</summary>

First 40 items of chatcmpl-0d9d5d925018fa663bef3cedaa9b8331:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
relates
to
the
'pymdownx.highlight'
extension
2.
Currently,
when
highlighting
code,
it
generates
CSS
classes
like
"language-mylanguage"
3.
The
user
wants
to
be
able
to
remove
the
"language-"
prefix
so
that
only
```

First 40 items of chatcmpl-59aafb09bb7679fb7733238b9fe6c536:

```text
Let
me
analyze
the
issue
description:
1.
The
issue
is
about
the
pymdownx.highlight
extension
2.
Currently
it
generates
CSS
classes
with
the
format
"language-mylanguage"
3.
The
user
wants
to
be
able
to
remove
the
"language-"
prefix
to
just
have
"mylanguage"
```

</details>

<details><summary>J = 0.463 · related · chatcmpl-bae5ba6910c34276b1c1813def39b7c2 (dwavesystems__dwave-cloud-client-309, resolved 1) · chatcmpl-d2bdaf1f9929b3d78847460db5f0aa72 (dwavesystems__dwave-cloud-client-309, resolved 0)</summary>

First 40 items of chatcmpl-bae5ba6910c34276b1c1813def39b7c2:

```text
view
/workspace/dwavesystems__dwave-cloud-client__0.5
/workspace/dwavesystems__dwave-cloud-client__0.5/README.rst
view
/workspace/dwavesystems__dwave-cloud-client__0.5/issue.md
view
/workspace/dwavesystems__dwave-cloud-client__0.5/dwave/cloud
view
/workspace/dwavesystems__dwave-cloud-client__0.5/requirements.txt
view
python
-m
pytest
tests/
-v
grep
-r
"tags"
--include="*.py"
.
grep
-r
"solver"
--include="*.py"
.
|
grep
-i
tag
grep
-r
"tag"
--include="*.py"
.
/workspace/dwavesystems__dwave-cloud-client__0.5/dwave/cloud/solver.py
view
grep
-n
"tag"
dwave/cloud/solver.py
```

First 40 items of chatcmpl-d2bdaf1f9929b3d78847460db5f0aa72:

```text
view
/workspace
/workspace/dwavesystems__dwave-cloud-client__0.5
view
/workspace/dwavesystems__dwave-cloud-client__0.5/issue.md
view
/workspace/dwavesystems__dwave-cloud-client__0.5/README.rst
view
/workspace/dwavesystems__dwave-cloud-client__0.5/dwave/cloud
view
pwd
/workspace/dwavesystems__dwave-cloud-client__0.5/requirements.txt
view
python
-m
pytest
tests/
-v
find
.
-name
"*.py"
-exec
grep
-l
"solver\|Solver"
{}
\;
/workspace/dwavesystems__dwave-cloud-client__0.5/dwave/cloud/solver.py
view
grep
-n
"tag"
dwave/cloud/solver.py
grep
-n
"properties"
dwave/cloud/solver.py
grep
-n
```

</details>

<details><summary>J = 0.463 · related · chatcmpl-1d9eb75c6240f09a05d551d393a2aa76 (joblib__joblib-985, resolved 1) · chatcmpl-e9984231c5151bf3fd0a59e65087c373 (joblib__joblib-985, resolved 1)</summary>

First 40 items of chatcmpl-1d9eb75c6240f09a05d551d393a2aa76:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
user
is
using
joblib's
parallel_backend
with
threading
backend
and
setting
n_jobs=4
2.
Inside
a
class
method,
they
call
effective_n_jobs(None)
expecting
it
to
respect
the
backend's
n_jobs
setting
3.
However,
effective_n_jobs
```

First 40 items of chatcmpl-e9984231c5151bf3fd0a59e65087c373:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
user
is
using
joblib's
`effective_n_jobs`
function
to
determine
the
number
of
jobs
to
use
2.
They're
using
a
`parallel_backend("threading",
n_jobs=4)`
context
manager
3.
When
they
call
`effective_n_jobs(None)`
inside
the
context,
```

</details>

<details><summary>J = 0.463 · related · chatcmpl-293b0a7f30943b7d8e7d223ef676ae94 (repobee__repobee-174, resolved 1) · chatcmpl-ee9ed120a3f2a2c9f5e5e860830657e3 (repobee__repobee-174, resolved 1)</summary>

First 40 items of chatcmpl-293b0a7f30943b7d8e7d223ef676ae94:

```text
Let
me
analyze
the
issue
description:
1.
The
goal
is
to
"Remove
type
validation
and
associated
tests"
2.
The
reason
is
that
it's
"only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
suggest
using
MyPy
```

First 40 items of chatcmpl-ee9ed120a3f2a2c9f5e5e860830657e3:

```text
Let
me
analyze
the
issue
description:
1.
The
goal
is
to
"Remove
type
validation
and
associated
tests"
2.
The
reason
is
that
it's
"only
in
the
way
and
adds
very
little
in
terms
of
safety"
3.
They
suggest
using
MyPy
```

</details>

<details><summary>J = 0.463 · related · chatcmpl-53672ff6a980e8d7e9972c30612e1308 (PyPSA__PyPSA-891, resolved 1) · chatcmpl-744870c99b888609632fea71f83ad069 (PyPSA__PyPSA-891, resolved 1)</summary>

First 40 items of chatcmpl-53672ff6a980e8d7e9972c30612e1308:

```text
Let
me
analyze
the
issue
description
carefully:
1.1
Code
snippets
and
conventions:
-
The
example
uses
PyPSA's
spatial
clustering
functionality
-
It
creates
a
network
using
`pypsa.examples.scigrid_de()`
-
Uses
`n.set_investment_periods([2020,
2030])`
to
set
investment
periods
-
Two
clustering
methods
are
```

First 40 items of chatcmpl-744870c99b888609632fea71f83ad069:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code
snippets
analysis:
-
The
code
uses
PyPSA's
spatial
clustering
functions
-
It
creates
a
network
using
`pypsa.examples.scigrid_de()`
-
It
sets
investment
periods
using
`n.set_investment_periods([2020,
2030])`
-
It
uses
two
clustering
```

</details>

<details><summary>J = 0.462 · related · chatcmpl-4ae9c060a198bcc01433a985e4e5adb4 (mir-dataset-loaders__mirdata-249, resolved 0) · chatcmpl-4c6788f9bbd2ea0412b3b2aba38bbb5f (mir-dataset-loaders__mirdata-249, resolved 0)</summary>

First 40 items of chatcmpl-4ae9c060a198bcc01433a985e4e5adb4:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
about
GTZAN
genre
dataset
naming
inconsistency
2.
The
gtzan_genre
module
uses
'hiphop'
(without
hyphen)
3.
The
jams
gtzan_genre
schema
expects
'hip-hop'
(with
hyphen)
4.
This
causes
jams
files
to
not
```

First 40 items of chatcmpl-4c6788f9bbd2ea0412b3b2aba38bbb5f:

```text
Let
me
break
down
the
issue
description:
1.
The
problem
is
about
GTZAN
genre
dataset
naming
inconsistency
2.
The
gtzan_genre
module
uses
'hiphop'
(without
hyphen)
3.
But
the
JAMS
gtzan_genre
schema
expects
'hip-hop'
(with
hyphen)
4.
This
causes
JAMS
files
```

</details>

<details><summary>J = 0.462 · related · chatcmpl-3ce537b6babe86ef15473280b22074b8 (joblib__joblib-985, resolved 1) · chatcmpl-e9984231c5151bf3fd0a59e65087c373 (joblib__joblib-985, resolved 1)</summary>

First 40 items of chatcmpl-3ce537b6babe86ef15473280b22074b8:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
user
is
using
joblib's
`effective_n_jobs`
function
to
determine
how
many
jobs
to
use
for
parallel
processing
2.
They're
using
a
`parallel_backend("threading",
n_jobs=4)`
context
manager
to
set
up
threading
with
4
```

First 40 items of chatcmpl-e9984231c5151bf3fd0a59e65087c373:

```text
Let
me
carefully
read
and
understand
the
issue:
1.
The
user
is
using
joblib's
`effective_n_jobs`
function
to
determine
the
number
of
jobs
to
use
2.
They're
using
a
`parallel_backend("threading",
n_jobs=4)`
context
manager
3.
When
they
call
`effective_n_jobs(None)`
inside
the
context,
```

</details>

<details><summary>J = 0.462 · related · chatcmpl-92141ca8bba3d0816709d23f33ccd7d1 (bridgecrewio__checkov-2214, resolved 0) · chatcmpl-f2e620c064af0b545c29cde317a453af (bridgecrewio__checkov-2214, resolved 0)</summary>

First 40 items of chatcmpl-92141ca8bba3d0816709d23f33ccd7d1:

```text
Let
me
analyze
the
issue
description:
1.1
Code/config
snippets
analysis:
-
The
example
shows
Terraform
configuration
for
Azure
App
Service
-
It
sets
`dotnet_framework_version
=
"v6.0"`
in
the
site_config
block
-
This
should
not
trigger
a
warning
for
CKV_AZURE_80
1.2
```

First 40 items of chatcmpl-f2e620c064af0b545c29cde317a453af:

```text
Let
me
analyze
the
issue
description:
1.1
Code/config
snippets
analysis:
-
The
example
shows
Terraform
configuration
for
Azure
App
Service
-
It
sets
`dotnet_framework_version
=
"v6.0"`
in
the
`site_config`
block
-
This
should
be
valid
since
.NET
6.0
is
the
```

</details>

<details><summary>J = 0.459 · related · chatcmpl-d3bbe409be4b6e0fa4132c2943fc4539 (matchms__matchms-539, resolved 1) · chatcmpl-e9a5f77cfe1ed821386fe6c458641397 (matchms__matchms-539, resolved 1)</summary>

First 40 items of chatcmpl-d3bbe409be4b6e0fa4132c2943fc4539:

```text
Let
me
carefully
read
the
issue
description
to
understand
the
problem:
1.1
Code/config
snippets
analysis:
-
The
issue
involves
reading
a
spectrum
with
PEPMASS
information:
"PEPMASS:
(981.54,
None)"
-
This
is
in
MGF
format
(Mascot
Generic
Format)
-
The
PEPMASS
```

First 40 items of chatcmpl-e9a5f77cfe1ed821386fe6c458641397:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
bug
occurs
when
reading
a
spectrum
with
pepmass
information
that
fails
upon
metadata
harmonization
2.
The
PEPMASS
field
contains:
"PEPMASS:
(981.54,
None)"
3.
This
suggests
that
the
pepmass
parsing
logic
doesn't
```

</details>

### Nearest to t

<details><summary>J = 0.200 · related · chatcmpl-12fb3d3ffb7d70c85e18ea8481f2a238 (meejah__txtorcon-213, resolved 1) · chatcmpl-46dc3627ffb094ee0e2d62c8fcbdc313 (meejah__txtorcon-213, resolved 1)</summary>

First 40 items of chatcmpl-12fb3d3ffb7d70c85e18ea8481f2a238:

```text
view
/workspace
/workspace/meejah__txtorcon__0.18
view
/workspace/meejah__txtorcon__0.18/README.rst
view
/workspace/meejah__txtorcon__0.18/requirements.txt
view
python
-c
"import
sys;
print(sys.version)"
pip
install
-e
.
python
-m
pytest
test/
-v
Looking
at
the
issue,
I
need
to
understand
how
event
listeners
work
in
txtorcon.
Since
this
is
a
```

First 40 items of chatcmpl-46dc3627ffb094ee0e2d62c8fcbdc313:

```text
view
/workspace
/workspace/meejah__txtorcon__0.18
view
/workspace/meejah__txtorcon__0.18/README.rst
view
pwd
/workspace/meejah__txtorcon__0.18/setup.py
view
/workspace/meejah__txtorcon__0.18/requirements.txt
view
python
-m
pytest
test/
-v
Looking
at
the
issue
description,
I
need
to
understand
how
event
listeners/handlers
work
in
txtorcon.
The
question
is
about
what
happens
when
an
event
```

</details>

<details><summary>J = 0.200 · hard negative · chatcmpl-161271eb56d8e89bbabc71e81b92b763 (zopefoundation__zope.component-55, resolved 1) · chatcmpl-28ade3fd1180213aa1b7526ad42b3f81 (zopefoundation__zope.component-58, resolved 1)</summary>

First 40 items of chatcmpl-161271eb56d8e89bbabc71e81b92b763:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
in
`zcml.py`
file
2.
When
registering
components
in
ZCML
with
both
a
factory
and
permission
set,
the
factory
should
be
wrapped
with
`zope.component.security.protectedFactory`
3.
However,
this
wrapping
is
happening
for
```

First 40 items of chatcmpl-28ade3fd1180213aa1b7526ad42b3f81:

```text
Let
me
carefully
read
and
understand
the
issue
description:
1.
The
issue
is
about
the
`<subscriber
/>`
directive
in
ZCML
(Zope
Configuration
Markup
Language)
2.
According
to
the
documentation,
like
the
`<adapter
/>`
directive,
the
`<subscriber
/>`
directive
should
be
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-1cd9c224f2c38ac28a3ea40b29904854 (polygraph-python__polygraph-19, resolved 1) · chatcmpl-4d7ca14dba9b1374f4ac8f6bb865a084 (polygraph-python__polygraph-19, resolved 0)</summary>

First 40 items of chatcmpl-1cd9c224f2c38ac28a3ea40b29904854:

```text
view
/workspace
/workspace/polygraph-python__polygraph__unknown
view
/workspace/polygraph-python__polygraph__unknown/README.md
view
/workspace/polygraph-python__polygraph__unknown/polygraph
view
/workspace/polygraph-python__polygraph__unknown/polygraph/__init__.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/__init__.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/definitions.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/basic_type.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/object_type.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/decorators.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/enum.py
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/tests
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types/tests/test_types.py
view
find
.
-type
f
-name
"*.py"
-exec
grep
-l
"cache\|meta"
{}
\;
grep
-rn
```

First 40 items of chatcmpl-4d7ca14dba9b1374f4ac8f6bb865a084:

```text
view
/workspace
/workspace/polygraph-python__polygraph__unknown
view
/workspace/polygraph-python__polygraph__unknown/README.md
view
/workspace/polygraph-python__polygraph__unknown/polygraph
view
/workspace/polygraph-python__polygraph__unknown/polygraph/types
view
pwd
/workspace/polygraph-python__polygraph__unknown/setup.py
view
find
.
-name
"*.cfg"
-o
-name
"*.ini"
-o
-name
"tox.ini"
-o
-name
"pytest.ini"
/workspace/polygraph-python__polygraph__unknown/setup.cfg
view
python
-m
pytest
polygraph/types/tests/
-v
grep
-r
"meta"
--include="*.py"
.
grep
-r
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-1d31e3b9e62ec595a290b240c267cab4 (NREL__hescore-hpxml-103, resolved 1) · chatcmpl-2aa00fc224089a162dfb8db5e21c774b (NREL__hescore-hpxml-103, resolved 1)</summary>

First 40 items of chatcmpl-1d31e3b9e62ec595a290b240c267cab4:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
about
cooling-only
mini
splits
2.
Currently,
they
are
mapped
to
`split_dx`
according
to
the
documentation
3.
HEScore
has
been
updated
to
handle
cooling-only
mini
splits
properly
4.
They
should
now
```

First 40 items of chatcmpl-2aa00fc224089a162dfb8db5e21c774b:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
about
cooling-only
mini
splits
being
mapped
incorrectly
2.
Currently
they
are
mapped
to
`split_dx`
according
to
the
documentation
link
provided
3.
HEScore
has
been
updated
to
handle
cooling-only
mini
splits
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-2a280f7b18c7207783c137843e153c54 (PyCQA__pyflakes-754, resolved 1) · chatcmpl-9cd97f553de8c593137f4a4741e906e4 (PyCQA__pyflakes-754, resolved 1)</summary>

First 40 items of chatcmpl-2a280f7b18c7207783c137843e153c54:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
problem
involves
forward
references
in
type
annotations
within
classes
defined
inside
functions
2.
There's
inconsistent
behavior
between:
-
Classes
defined
at
module
level
vs
classes
defined
inside
functions
-
Different
ways
```

First 40 items of chatcmpl-9cd97f553de8c593137f4a4741e906e4:

```text
Let
me
carefully
read
and
understand
the
problem:
1.
The
issue
involves
forward
references
in
type
annotations
within
classes
defined
inside
functions
2.
In
the
outer
scope
(module
level),
both
`Optional["Model2"]`
and
`"Optional[Model2]"`
work
correctly
3.
But
inside
a
function,
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-2aa00fc224089a162dfb8db5e21c774b (NREL__hescore-hpxml-103, resolved 1) · chatcmpl-c099f85938fc73f53c0b63bef7bf0f07 (NREL__hescore-hpxml-103, resolved 1)</summary>

First 40 items of chatcmpl-2aa00fc224089a162dfb8db5e21c774b:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
about
cooling-only
mini
splits
being
mapped
incorrectly
2.
Currently
they
are
mapped
to
`split_dx`
according
to
the
documentation
link
provided
3.
HEScore
has
been
updated
to
handle
cooling-only
mini
splits
```

First 40 items of chatcmpl-c099f85938fc73f53c0b63bef7bf0f07:

```text
Let
me
analyze
the
issue
description:
1.
The
problem
is
about
cooling-only
mini
splits
being
mapped
incorrectly
2.
Currently
they
are
mapped
to
`split_dx`
3.
HEScore
has
been
updated
to
handle
cooling-only
mini
splits
properly
4.
They
should
now
be
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-349d0187fe2bb30e290d5b1d24e2ba6b (datalad__datalad-4575, resolved 0) · chatcmpl-5f16b2ed1bd38f9eab1b2711c262998c (datalad__datalad-4575, resolved 0)</summary>

First 40 items of chatcmpl-349d0187fe2bb30e290d5b1d24e2ba6b:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
problem:
When
running
a
specific
command
to
search
for
missing
local
annex
files,
users
are
seeing
unwanted
"[INFO]
Clear
progress
bars"
and
"[INFO]
Refresh
progress
bars"
messages
in
version
0.13.0rc2
that
```

First 40 items of chatcmpl-5f16b2ed1bd38f9eab1b2711c262998c:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code/config
snippets
analysis:
-
The
user
runs
a
bash
command:
`echo
"Searching
for
missing
files"
;
datalad
-f
json
status
--recursive
--annex
all
|
jq
'.
|
select(.has_content
==
false).path'`
-
```

</details>

<details><summary>J = 0.200 · related · chatcmpl-40a7b176c8a57198f6dd1386508f723d (iterative__dvc-3316, resolved 0) · chatcmpl-8be2ad33146819c41892c49e360d9dac (iterative__dvc-3316, resolved 0)</summary>

First 40 items of chatcmpl-40a7b176c8a57198f6dd1386508f723d:

```text
Let
me
analyze
the
issue
description
carefully:
1.
The
problem
is
that
`dvc
push
-q`
(quiet
mode)
is
not
completely
quiet
2.
It
still
shows
tqdm
progress
bars
during
file
transfers
3.
This
is
problematic
for
users
with
many
files
```

First 40 items of chatcmpl-8be2ad33146819c41892c49e360d9dac:

```text
Let
me
carefully
analyze
the
issue:
1.1
Code/config
snippets:
The
user
is
running
'dvc
push
-q'
which
should
be
a
quiet
mode
command.
1.2
Technical
details:
-
Command:
dvc
push
-q
-
Issue:
tqdm
progress
bars
are
still
showing
-
```

</details>

<details><summary>J = 0.200 · hard negative · chatcmpl-4470d2876306dfc7787832d96bf96586 (zopefoundation__zope.component-55, resolved 1) · chatcmpl-7704eb3d9c1b63f35e3a772434169dae (zopefoundation__zope.component-58, resolved 1)</summary>

First 40 items of chatcmpl-4470d2876306dfc7787832d96bf96586:

```text
view
/workspace
/workspace/zopefoundation__zope.component__4.6
view
/workspace/zopefoundation__zope.component__4.6/src
view
/workspace/zopefoundation__zope.component__4.6/README.rst
view
/workspace/zopefoundation__zope.component__4.6/setup.py
view
/workspace/zopefoundation__zope.component__4.6/tox.ini
view
python
-m
unittest
discover
-s
src
-v
pip
install
-e
.
pip
install
-e
".[test]"
python
-m
unittest
discover
-s
src
/workspace/zopefoundation__zope.component__4.6/src/zope/component
view
/workspace/zopefoundation__zope.component__4.6/src/zope/component/zcml.py
view
grep
-n
"protectedFactory\|proxify"
```

First 40 items of chatcmpl-7704eb3d9c1b63f35e3a772434169dae:

```text
Let
me
carefully
read
and
understand
the
issue
description:
1.
The
issue
is
about
the
`<subscriber
/>`
directive
in
ZCML
(Zope
Configuration
Markup
Language)
2.
According
to
the
docs,
like
the
`<adapter
/>`
directive,
the
`<subscriber
/>`
directive
should
be
```

</details>

<details><summary>J = 0.200 · hard negative · chatcmpl-472990bf1a531cf8260410dfb0bc81d8 (tobymao__sqlglot-799, resolved 0) · chatcmpl-f3136aa3091542e51b639001ccd11412 (tobymao__sqlglot-2049, resolved 0)</summary>

First 40 items of chatcmpl-472990bf1a531cf8260410dfb0bc81d8:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code
snippets
analysis:
-
The
failing
SQL
query
is:
`SELECT
ARRAY(SELECT
x
*
2
FROM
UNNEST([0,
1])
AS
x)`
-
The
simplified
version
is:
`SELECT
ARRAY(SELECT
x
FROM
UNNEST([0,
1])
AS
```

First 40 items of chatcmpl-f3136aa3091542e51b639001ccd11412:

```text
Let
me
carefully
read
and
understand
the
issue:
1.1
Code/config
snippets
analysis:
-
The
issue
shows
a
MERGE
statement
with
`mytable
WITH
(HOLDLOCK)
AS
T`
-
HOLDLOCK
is
a
table
hint
that
should
be
parsed
correctly
-
Without
the
WITH
```

</details>

## 5. Patch items (solution similarity)

These do not take part in the choice of the behaviour item. Trajectories with an empty patch are left out.

| Candidate | size min / p10 / p50 / p90 / max | AUC HN | AUC UN | Repeat | wAUC HN | AUC RR vs RU [99% CI] |
|---|---|---|---|---|---|---|
| patch_all | 1 / 8 / 87 / 534 / 2600 | 0.882 | 0.928 | 0.124 | 0.846 | 0.598 [0.571, 0.625] |
| patch_no_new | 0 / 2 / 13 / 48 / 1225 | 0.898 | 0.908 | 0.112 | 0.898 | 0.734 [0.705, 0.764] |

| Candidate | J related mean / median | J hard negative mean / median | J unrelated mean / median | J RR mean / median | J RU mean / median | J UU mean / median |
|---|---|---|---|---|---|---|
| patch_all | 0.140 / 0.047 | 0.006 / 0.000 | 0.002 / 0.000 | 0.179 / 0.064 | 0.110 / 0.038 | 0.118 / 0.038 |
| patch_no_new | 0.250 / 0.143 | 0.002 / 0.000 | 0.000 / 0.000 | 0.372 / 0.250 | 0.136 / 0.077 | 0.207 / 0.105 |

Share of pairs with set J at or above each level (deduplication and LSH need pairs above t):

| Candidate | related J ≥ 0.3 | related J ≥ 0.5 | related J ≥ 0.7 | hard negative J ≥ 0.3 | hard negative J ≥ 0.5 | hard negative J ≥ 0.7 |
|---|---|---|---|---|---|---|
| patch_all | 0.149 | 0.090 | 0.051 | 0.000 | 0.000 | 0.000 |
| patch_no_new | 0.294 | 0.174 | 0.109 | 0.001 | 0.000 | 0.000 |

Outcome claims (Q14): patch_all: no outcome difference is found with this item; patch_no_new: **an outcome difference is found**.

## 6. Run

Run time: 232 s. Machine: arm, 10 CPUs, Darwin arm64, Python 3.14.7, DuckDB 2.0.1, NumPy 2.5.5.

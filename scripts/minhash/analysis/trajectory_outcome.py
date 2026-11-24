# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# /// script
# requires-python = ">=3.10"
# dependencies = ["duckdb", "numpy", "scipy"]
# ///

# What separates resolved from unresolved attempts at the same task? Compares
# trace signals and the agreement of final patches, inside each task instance of
# the MH-15 sample (every one has both outcomes), so task difficulty cancels out.
# Run from the repo root:
#   uv run scripts/minhash/analysis/trajectory_outcome.py [--parquet PATH] [--out PATH]
# It writes scripts/minhash/results/trajectory_outcome.md.

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path

import duckdb
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from trajectory_dedup import PARQUET, patch_lines  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "results" / "trajectory_outcome.md"
FINISHED_RE = re.compile(r"\[Command finished with exit code (-?\d+)\]\s*$")
ADMITS_RE = re.compile(r"\b(not (?:fully|completely)|unable|could not|couldn't|still fail|remain(?:s|ing)? (?:fail|issue)"
                       r"|partial|workaround|limitation)\b")


def jac(a, b):
    return len(a & b) / len(a | b) if a | b else 1.0


def changed_files(patch):
    """Existing files that the patch changes (new files are the agent's scratch scripts)."""
    files, new, cur = set(), set(), None
    for line in patch.splitlines():
        if line.startswith("diff --git "):
            cur = line.split(" b/", 1)[-1]
            files.add(cur)
        elif line.startswith("new file mode"):
            new.add(cur)
    return files - new


def trace_signals(traj, exit_status):
    """Signals that the agent's own trace gives about its outcome."""
    replies = {m["tool_call_id"]: (m["content"] or "") for m in traj if m["role"] == "tool"}
    calls = [c for m in traj if m["role"] == "assistant" for c in (m["tool_calls"] or [])]
    pytest, edits, failed_edits, last_edit, finish = [], 0, 0, None, ""
    for k, c in enumerate(calls):
        fn = c["function"]["name"]
        try:
            a = json.loads(c["function"]["arguments"] or "{}")
        except json.JSONDecodeError:
            a = {}
        a = a if isinstance(a, dict) else {}
        reply = replies.get(c["id"], "")
        if fn == "execute_bash":
            m = FINISHED_RE.search(reply)
            if "pytest" in str(a.get("command", "")) and m:
                pytest.append((k, int(m.group(1))))
        elif fn == "str_replace_editor" and a.get("command") in ("str_replace", "insert"):
            edits += 1
            last_edit = k
            failed_edits += reply.startswith("ERROR") or "No replacement was performed" in reply
        elif fn == "finish":
            finish = str(a.get("message", ""))
    nan = float("nan")
    return {
        "last pytest run passes": float(pytest[-1][1] == 0) if pytest else nan,
        "share of pytest runs that fail": float(np.mean([c != 0 for _, c in pytest])) if pytest else nan,
        "pytest run after the last edit": float(bool(pytest) and last_edit is not None and pytest[-1][0] > last_edit),
        "hit the iteration cap": float("maximum iteration" in (exit_status or "")),
        "called finish": float(bool(finish)),
        "finish message admits a problem": float(bool(ADMITS_RE.search(finish.lower()))) if finish else nan,
        "edits (str_replace, insert)": float(edits),
        "failed edits": float(failed_edits),
        "tool calls": float(len(calls)),
    }


def within_auc(score, res, by_task):
    """P(score of a resolved attempt > score of an unresolved attempt of the same task); NaN scores left out."""
    num = den = 0.0
    for ix in by_task.values():
        r = [score[i] for i in ix if res[i] == 1 and not np.isnan(score[i])]
        u = np.sort([score[i] for i in ix if res[i] == 0 and not np.isnan(score[i])])
        for x in r:
            num += np.searchsorted(u, x, "left") + 0.5 * (np.searchsorted(u, x, "right") - np.searchsorted(u, x, "left"))
            den += len(u)
    return num / den if den else float("nan")


def self_check():
    assert jac({1, 2}, {2, 3}) == 1 / 3 and jac(set(), set()) == 1.0
    p = "diff --git a/x.py b/x.py\n--- a/x.py\n+++ b/x.py\n+a\ndiff --git a/n.py b/n.py\nnew file mode 100644\n+b\n"
    assert changed_files(p) == {"x.py"}
    by = {"t": [0, 1, 2]}
    assert within_auc(np.array([2.0, 1.0, 1.0]), np.array([1, 0, 1]), by) == 0.75


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parquet", default=str(PARQUET))
    ap.add_argument("--instances", type=int, default=1000)
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()
    self_check()
    con = duckdb.connect()
    src = f"read_parquet('{args.parquet}')"
    rows = con.execute(f"""
        WITH chosen AS (
          SELECT instance_id FROM {src} GROUP BY instance_id
          HAVING bool_or(resolved = 1) AND bool_or(resolved = 0)
          ORDER BY md5(instance_id) LIMIT {args.instances})
        SELECT t.trajectory_id, t.instance_id, t.resolved, t.exit_status, t.model_patch, t.trajectory
        FROM {src} AS t JOIN chosen USING (instance_id)
        ORDER BY md5(t.instance_id), t.trajectory_id""").fetchall()
    tid = [r[0] for r in rows]
    res = np.array([int(r[2]) for r in rows])
    by_task = defaultdict(list)
    for i, r in enumerate(rows):
        by_task[r[1]].append(i)
    lines = [patch_lines(r[4] or "") for r in rows]
    files = [changed_files(r[4] or "") for r in rows]
    sig = defaultdict(lambda: np.full(len(rows), np.nan))
    for i, r in enumerate(rows):
        for k, v in trace_signals(r[5], r[3]).items():
            sig[k][i] = v
    W = [f"Sample: {len(by_task)} task instances, {len(rows)} trajectories ({res.sum()} resolved). Every task instance has both outcomes, "
         "so each comparison below is between attempts at the same task. Within-task AUC: P(resolved attempt scores higher than an "
         "unresolved attempt of the same task); 0.5 is chance.", ""]

    W += ["## 1. Trace signals", "", "| Signal | resolved mean | unresolved mean | within-task AUC |", "|---|---|---|---|"]
    for k, x in sig.items():
        W.append(f"| {k} | {np.nanmean(x[res == 1]):.3f} | {np.nanmean(x[res == 0]):.3f} | {within_auc(x, res, by_task):.3f} |")

    W += ["", "## 2. Agreement of final patches", "",
          "Patch lines: MH-E1 `patch_no_new` (changed lines of existing files). Patch files: the existing files that the patch changes.", "",
          "| Pair inside one task | pairs | patch-line J mean | share ≥ 0.8 | patch-file J mean |", "|---|---|---|---|---|"]
    cls = defaultdict(lambda: ([], []))
    for ix in by_task.values():
        for a, b in combinations(ix, 2):
            k = "resolved – resolved" if res[a] and res[b] else "unresolved – unresolved" if not (res[a] or res[b]) else "resolved – unresolved"
            cls[k][0].append(jac(lines[a], lines[b]))
            cls[k][1].append(jac(files[a], files[b]))
    for k in ("resolved – resolved", "resolved – unresolved", "unresolved – unresolved"):
        x, f = np.array(cls[k][0]), np.array(cls[k][1])
        W.append(f"| {k} | {len(x)} | {x.mean():.3f} | {np.mean(x >= 0.8):.3f} | {f.mean():.3f} |")

    hit, nofile = {0: [], 1: []}, {0: 0, 1: 0}
    twins = []
    for ix in by_task.values():
        for i in ix:
            others = [j for j in ix if j != i and res[j] == 1]
            if not others:
                continue
            ref = set().union(*(files[j] for j in others))
            hit[res[i]].append(bool(files[i] & ref))
            nofile[res[i]] += not files[i]
            if res[i] == 0 and lines[i] and any(lines[i] == lines[j] for j in others):
                twins.append(tid[i])
    W += ["", "Where the fix goes (leave-one-out: the files that the other resolved attempts of the task change):", ""]
    for r, nm in ((1, "Resolved"), (0, "Unresolved")):
        W.append(f"- {nm}: the patch changes a file that another resolved attempt changes: {np.mean(hit[r]):.3f} "
                 f"(n {len(hit[r])}; no existing file changed: {nofile[r]}).")
    W.append(f"- Unresolved attempts with exactly the changed lines of a resolved attempt of the same task: {len(twins)} of {int((res == 0).sum())}.")

    cons = np.zeros(len(rows))
    near = np.zeros(len(rows))
    for ix in by_task.values():
        for i in ix:
            o = [j for j in ix if j != i]
            cons[i] = np.mean([jac(lines[i], lines[j]) for j in o])
            r = [jac(lines[i], lines[j]) for j in o if res[j] == 1]
            u = [jac(lines[i], lines[j]) for j in o if res[j] == 0]
            near[i] = (max(r) if r else 0.0) - (max(u) if u else 0.0)
    n_lines = np.array([-len(x) for x in lines], float)
    W += ["", "## 3. Detecting the outcome from the patch", "", "| Score | uses labels of other attempts | within-task AUC |", "|---|---|---|",
          f"| Mean patch-line J to the other attempts of the task (consensus) | no | {within_auc(cons, res, by_task):.3f} |",
          f"| Nearest resolved attempt minus nearest unresolved attempt (patch-line J) | yes | {within_auc(near, res, by_task):.3f} |",
          f"| Fewer changed lines | no | {within_auc(n_lines, res, by_task):.3f} |"]
    rnd = np.mean([res[ix].mean() for ix in by_task.values()])
    best = np.mean([res[max(ix, key=lambda i: (cons[i], tid[i]))] for ix in by_task.values()])
    worst = np.mean([res[min(ix, key=lambda i: (cons[i], tid[i]))] for ix in by_task.values()])
    W += ["", f"Pick one attempt per task: a random one is resolved with probability {rnd:.3f}; the one with the highest consensus "
          f"{best:.3f}; the one with the lowest {worst:.3f} (ties broken by trajectory id)."]

    head = Path(__file__).read_text().split("\n# /// script")[0].replace("# ", "").replace("#", "")
    text = ("<!--\n" + head.strip() + "\n-->\n\n# Trajectory outcome: trace signals against patch agreement\n\n"
            "Written by `uv run scripts/minhash/analysis/trajectory_outcome.py`. "
            "The discussion is in section 8 of `scripts/minhash/analysis/trajectory_dedup_findings.md`.\n\n" + "\n".join(W) + "\n")
    Path(args.out).write_text(text)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()

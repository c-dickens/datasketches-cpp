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
# dependencies = ["duckdb", "numpy", "xxhash"]
# ///

# MH-E item analysis (MH-E1). Measures the candidate MinHash items of a
# trajectory with exact Jaccard on the MH-15 sample, and applies the fixed
# decision rules of the ticket. Run from the repo root:
#   uv run scripts/minhash/analysis/mhe_explore.py [--instances N] [--parquet PATH]
# It reads scripts/minhash/data/trajectories.parquet (or PATH) and writes
# scripts/minhash/results/mhe_explore.md.

import heapq
import json
import os
import platform
import re
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import duckdb
import numpy as np
import xxhash

HERE = Path(__file__).resolve().parent.parent
PARQUET = HERE / "data" / "trajectories.parquet"
OUT = HERE / "results" / "mhe_explore.md"
N_PAIRS = 50_000
N_BOOT = 2000
TEXT_WIDTHS = [1, 2, 3, 5]
NEWS_WIDTHS = [1, 2]
BEHAVIOUR = (["files", "commands"] + [f"steps_w{w}" for w in range(1, 6)]
             + [f"action_text_w{w}" for w in TEXT_WIDTHS] + [f"action_text_news_w{w}" for w in NEWS_WIDTHS])
J_LEVELS = [0.3, 0.5, 0.7]
PATCH = ["patch_all", "patch_no_new"]
K_CHOICES = [64, 128, 256]
BR_TABLE = {64: [(1, 64), (2, 32), (4, 16), (8, 8), (16, 4), (32, 2), (64, 1)],
            128: [(1, 128), (2, 64), (4, 32), (8, 16), (16, 8), (32, 4), (64, 2), (128, 1)],
            256: [(1, 256), (2, 128), (4, 64), (8, 32), (16, 16), (32, 8), (64, 4), (128, 2), (256, 1)]}

# ---------------------------------------------------------------- item rules

FINISHED_RE = re.compile(r"\[Command finished with exit code (-?\d+)\]\s*$")
COMPLETED_RE = re.compile(r"\[The command completed with exit code (-?\d+)\.\]")
TIMEOUT_MARKS = ("[The command has no new output after", "[The command timed out after",
                 'Your command "C-c" is NOT executed')
UPLOADED_RE = re.compile(r"<uploaded_files>\s*(/workspace/[^\s<]+)")


def strip_cd(cmd, root):
    """Q10: remove a leading `cd <root> &&` or `cd <root>;`."""
    m = re.match(r"\s*cd\s+" + re.escape(root) + r"/?\s*(&&|;)\s*", cmd)
    return cmd[m.end():] if m else cmd


def relative(text, root):
    """Q10: /workspace/<repo>/ becomes ./ everywhere; the root itself becomes ."""
    text = text.replace(root + "/", "./")
    return "." if text == root else text


def uniform_command(cmd, root):
    """Q10: the uniform form of a bash command, or None for C-c and empty commands."""
    cmd = re.sub(r"\s+", " ", relative(strip_cd(cmd, root), root)).strip()
    return None if cmd in ("", "C-c") else cmd


def exit_code(reply):
    """Q11: the exit code, from the marker at the end, else the last other marker."""
    m = FINISHED_RE.search(reply)
    if m:
        return int(m.group(1))
    found = COMPLETED_RE.findall(reply)
    return int(found[-1]) if found else None


def bash_symbol(cmd, root, reply):
    """Q11: execute_bash:<sub>:<outcome>."""
    words = re.sub(r"\s+", " ", strip_cd(cmd, root)).strip().split(" ")
    sub = words[0] if words[0] else "<empty>"
    if sub in ("python", "python3"):
        if words[1:3] == ["-m", "pytest"]:
            sub = "python:pytest"
        elif words[1:2] == ["-c"]:
            sub = "python:-c"
        else:
            sub = "python:script"
    code = exit_code(reply)
    if code is not None:
        outcome = "ok" if code == 0 else "fail"
    elif any(t in reply for t in TIMEOUT_MARKS):
        outcome = "timeout"
    else:
        outcome = "none"
    return f"execute_bash:{sub}:{outcome}"


def patch_items(patch, keep_new):
    """Q12: <file>:<sign>:<trimmed line> for each changed line of the diff."""
    items, current, is_new = [], None, False
    for line in patch.splitlines():
        if line.startswith("diff --git "):
            current = line.split(" b/", 1)[1] if " b/" in line else line[11:]
            is_new = False
        elif line.startswith("new file mode") and current is not None:
            is_new = True
        elif line.startswith(("+++", "---")):
            continue
        elif line[:1] in ("+", "-") and current is not None and (keep_new or not is_new):
            items.append(f"{current}:{line[0]}:{line[1:].strip()}")
    return items


def news_tokenize(text):
    """news20::tokenize: ASCII letters to lower case, split on every byte that is not an ASCII letter or digit."""
    return [t.decode() for t in re.findall(rb"[a-z0-9]+", text.encode().lower())]  # bytes.lower() is ASCII only


def shingles(words, w):
    return [" ".join(words[i:i + w]) for i in range(len(words) - w + 1)]


def occurrence_encode(items):
    """Q9: the n-th occurrence of x is the item x#n."""
    seen = Counter()
    out = []
    for x in items:
        seen[x] += 1
        out.append(f"{x}#{seen[x]}")
    return out


def auc(pos, neg, wpos=None, wneg=None):
    """P(pos > neg) + 0.5 P(pos == neg), with optional weights."""
    pos, neg = np.asarray(pos, float), np.asarray(neg, float)
    wpos = np.ones(len(pos)) if wpos is None else np.asarray(wpos, float)
    wneg = np.ones(len(neg)) if wneg is None else np.asarray(wneg, float)
    order = np.argsort(neg, kind="stable")
    neg, wneg = neg[order], wneg[order]
    cum = np.concatenate([[0.0], np.cumsum(wneg)])
    less = cum[np.searchsorted(neg, pos, "left")]
    leq = cum[np.searchsorted(neg, pos, "right")]
    den = wpos.sum() * wneg.sum()
    return float(np.sum(wpos * (less + 0.5 * (leq - less))) / den) if den > 0 else float("nan")


def rank(aucs, med_j):
    """Owner rule of 24 Nov 2026: (by AUC, within 0.02 of the best, rule order).

    Rule order: the candidates within 0.02 AUC of the best by median related J
    (ties: higher AUC), then the others by AUC. order[0] is the choice, order[1] the runner-up.
    """
    by_auc = sorted(aucs, key=lambda c: -aucs[c])
    near = [c for c in by_auc if aucs[c] >= aucs[by_auc[0]] - 0.02]
    order = sorted(near, key=lambda c: (-med_j[c], -aucs[c])) + [c for c in by_auc if c not in near]
    return by_auc, near, order


def self_check():
    root = "/workspace/o__r__1.0"
    assert strip_cd("cd /workspace/o__r__1.0 && python -m pytest", root) == "python -m pytest"
    assert strip_cd("cd /workspace/o__r__1.0; ls", root) == "ls"
    assert strip_cd("cd /tmp && ls", root) == "cd /tmp && ls"
    assert uniform_command("cd /workspace/o__r__1.0 &&  grep -n  x /workspace/o__r__1.0/a.py ", root) == "grep -n x ./a.py"
    assert uniform_command("C-c", root) is None and uniform_command("  ", root) is None
    mid = "[Command finished with exit code 3]\nmore output\n[Command finished with exit code 0]\n"
    assert exit_code(mid) == 0
    assert exit_code("x [Command finished with exit code 3] y") is None
    assert exit_code("[The command completed with exit code 2.]\nz\n[The command completed with exit code 1.]\nw") == 1
    assert bash_symbol("cd /workspace/o__r__1.0 && python -m pytest t", root, mid) == "execute_bash:python:pytest:ok"
    assert bash_symbol("python3 -c 'print(1)'", root, "[Command finished with exit code 1]") == "execute_bash:python:-c:fail"
    assert bash_symbol("python repro.py", root, "[The command timed out after 10.0 seconds.]") == "execute_bash:python:script:timeout"
    assert bash_symbol("ls", root, "no marker") == "execute_bash:ls:none"
    patch = ("diff --git a/x.py b/x.py\n--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n- a = 1 \n+ a = 2\n"
             "diff --git a/new.py b/new.py\nnew file mode 100644\n--- /dev/null\n+++ b/new.py\n+print(1)\n")
    assert patch_items(patch, True) == ["x.py:-:a = 1", "x.py:+:a = 2", "new.py:+:print(1)"]
    assert patch_items(patch, False) == ["x.py:-:a = 1", "x.py:+:a = 2"]
    assert occurrence_encode(["a", "b", "a", "a"]) == ["a#1", "b#1", "a#2", "a#3"]
    assert auc([3, 2], [1, 2]) == 0.875
    assert auc([1], [2]) == 0.0
    assert shingles(["a", "b", "c"], 2) == ["a b", "b c"]
    # The cases of news20::tokenize in scripts/minhash/test/loader_test.cpp, and one non-ASCII byte.
    assert news_tokenize("Hello, World! It's 2024") == ["hello", "world", "it", "s", "2024"]
    assert news_tokenize("  --  ") == [] and news_tokenize("") == []
    assert news_tokenize("a_b\tc\nD") == ["a", "b", "c", "d"]
    assert news_tokenize("caf\u00c9x") == ["caf", "x"]
    assert rank({"a": 0.99, "b": 0.98, "c": 0.90, "d": 0.95}, {"a": 0.1, "b": 0.4, "c": 0.9, "d": 0.5})[2] == ["b", "a", "d", "c"]
    # Plain Jaccard of the occurrence-encoded sets is the weighted Jaccard of the counts: 3 / 5.
    x, y = ["a", "a", "a", "b"], ["a", "b", "b", "c"]
    js, wjs = jaccards([item_counts(x), item_counts(y)], [(0, 1)])
    ejs, _ = jaccards([item_counts(occurrence_encode(x)), item_counts(occurrence_encode(y))], [(0, 1)])
    assert abs(js[0] - 2 / 3) < 1e-12 and abs(wjs[0] - 2 / 6) < 1e-12 and abs(ejs[0] - wjs[0]) < 1e-12


# ---------------------------------------------------------------- features

def item_counts(items):
    """Sorted distinct item ids (XXH64, seed 0, of the item text) and their counts."""
    if not items:
        return np.empty(0, np.uint64), np.empty(0, np.int64)
    h = np.fromiter((xxhash.xxh64_intdigest(x.encode()) for x in items), dtype=np.uint64, count=len(items))
    keys, counts = np.unique(h, return_counts=True)
    return keys, counts.astype(np.int64)


def featurize(traj, model_patch, facts):
    """The ordered item lists of every candidate for one trajectory."""
    root = None
    for m in traj[:3]:
        u = UPLOADED_RE.search(m["content"] or "") if m["role"] == "user" else None
        if u:
            root = u.group(1).rstrip("/")
            break
    root = root or "/workspace/<none>"
    replies = {m["tool_call_id"]: (m["content"] or "") for m in traj if m["role"] == "tool"}
    symbols, commands, files, words, news_words = [], [], [], [], []
    for m in traj[2:]:
        if m["role"] != "assistant" or not m["tool_calls"]:
            continue
        for call in m["tool_calls"]:
            fn = call["function"]["name"]
            try:
                args = json.loads(call["function"]["arguments"] or "{}")
            except json.JSONDecodeError:
                args = {}
            if not isinstance(args, dict):
                args = {}
            reply = replies.get(call["id"], "")
            for key, v in args.items():
                if isinstance(v, str):
                    text = strip_cd(v, root) if key == "command" and fn == "execute_bash" else v
                    words.extend(text.split())
                    news_words.extend(news_tokenize(text))
            if fn == "execute_bash":
                cmd = str(args.get("command", ""))
                facts["bash"] += 1
                facts["bash_cd"] += bool(re.match(r"\s*cd\s+" + re.escape(root) + r"/?\s*(&&|;)", cmd))
                first = re.sub(r"\s+", " ", strip_cd(cmd, root)).strip().split(" ")[0]
                facts["bash_python"] += first in ("python", "python3")
                facts["bash_exit"] += exit_code(reply) is not None
                facts["bash_finished_end"] += bool(FINISHED_RE.search(reply))
                facts["bash_finished_any"] += "[Command finished with exit code" in reply
                symbols.append(bash_symbol(cmd, root, reply))
                u = uniform_command(cmd, root)
                if u is not None:
                    commands.append(u)
            elif fn == "str_replace_editor":
                sub = str(args.get("command", ""))
                path = relative(str(args.get("path", "")), root)
                symbols.append(f"str_replace_editor:{sub}:{'err' if reply.startswith('ERROR') else 'ok'}")
                commands.append(f"editor:{sub}:{path}")
                files.append(path)
            elif fn == "think":
                symbols.append("think")
    bash_cmds = [c for c in commands if not c.startswith("editor:")]
    facts["traj"] += 1
    facts["traj_repeat_cmd"] += len(set(bash_cmds)) < len(bash_cmds)
    out = {"files": files, "commands": commands}
    for w in TEXT_WIDTHS:
        out[f"action_text_w{w}"] = shingles(words, w)
    for w in NEWS_WIDTHS:
        out[f"action_text_news_w{w}"] = shingles(news_words, w)
    for w in range(1, 6):
        out[f"steps_w{w}"] = shingles(symbols, w)
    patch = model_patch or ""
    out["patch_all"] = patch_items(patch, True)
    out["patch_no_new"] = patch_items(patch, False)
    facts["patch_lines"].append(len(out["patch_all"]))
    facts["patch_total_lines"].append(len(patch.splitlines()))
    return out


def load(instances, facts, parquet):
    con = duckdb.connect()
    con.execute("SET threads TO 4")
    src = f"read_parquet('{parquet}')"
    rows = con.execute(f"""
        WITH chosen AS (
          SELECT instance_id FROM {src} GROUP BY instance_id
          HAVING bool_or(resolved = 1) AND bool_or(resolved = 0)
          ORDER BY md5(instance_id) LIMIT {instances})
        SELECT t.trajectory_id, t.instance_id, t.repo, t.resolved, t.model_patch, t.trajectory
        FROM {src} AS t JOIN chosen USING (instance_id)
        ORDER BY md5(t.instance_id), t.trajectory_id""")
    meta = []
    feats_all = {c: [] for c in BEHAVIOUR + PATCH}
    first40 = {c: [] for c in BEHAVIOUR + PATCH}
    while True:
        batch = rows.fetchmany(200)
        if not batch:
            break
        for tid, iid, repo, resolved, patch, traj in batch:
            feats = featurize(traj, patch, facts)
            meta.append({"tid": tid, "iid": iid, "repo": repo, "resolved": int(resolved),
                         "empty_patch": not (patch or "").strip()})
            for c in feats_all:
                feats_all[c].append(item_counts(feats[c]))
                first40[c].append(feats[c][:40])
    return meta, feats_all, first40


# ---------------------------------------------------------------- pairs

def smallest_pairs(blocks, accept, n):
    """The n pairs (i, j), id_i < id_j, with the smallest xxh64 of "id_i|id_j", seed 1.

    blocks: lists of (id, index); a pair is formed inside one block, or across all if
    blocks is one list. accept(a, b) filters pairs.
    """
    heap = []  # max-heap of (-hash, i, j)
    for block in blocks:
        block = sorted(block)
        for x in range(len(block)):
            ida, ia = block[x]
            for idb, ib in block[x + 1:]:
                if not accept(ia, ib):
                    continue
                h = xxhash.xxh64_intdigest(f"{ida}|{idb}".encode(), seed=1)
                if len(heap) < n:
                    heapq.heappush(heap, (-h, ia, ib))
                elif h < -heap[0][0]:
                    heapq.heapreplace(heap, (-h, ia, ib))
    return sorted((-h, i, j) for h, i, j in heap)


def build_pairs(meta):
    by_inst, by_repo = defaultdict(list), defaultdict(list)
    for i, m in enumerate(meta):
        by_inst[m["iid"]].append(i)
        by_repo[m["repo"]].append((m["tid"], i))
    related = []
    for idx in by_inst.values():
        idx = sorted(idx, key=lambda i: meta[i]["tid"])
        related += [(idx[a], idx[b]) for a in range(len(idx)) for b in range(a + 1, len(idx))]
    hard = smallest_pairs(by_repo.values(), lambda a, b: meta[a]["iid"] != meta[b]["iid"], N_PAIRS)
    unrel = unrelated_pairs(meta, by_repo)
    return related, [(i, j) for _, i, j in hard], [(i, j) for _, i, j in unrel], by_inst, by_repo


def unrelated_pairs(meta, by_repo):
    """Pairs of different repos with the smallest hashes, without a heap push per pair."""
    allv = sorted((m["tid"], i) for i, m in enumerate(meta))
    repo = np.array([meta[i]["repo"] for _, i in allv], dtype=object)
    best = np.empty(0, dtype=np.uint64)
    best_i, best_j = np.empty(0, dtype=np.int64), np.empty(0, dtype=np.int64)
    cut = np.uint64(2**64 - 1)
    for x in range(len(allv) - 1):
        ida, ia = allv[x]
        h = np.fromiter((xxhash.xxh64_intdigest(f"{ida}|{allv[y][0]}".encode(), seed=1) for y in range(x + 1, len(allv))),
                        dtype=np.uint64, count=len(allv) - x - 1)
        keep = (h < cut) & (repo[x + 1:] != repo[x])
        if not keep.any():
            continue
        ys = np.nonzero(keep)[0] + x + 1
        best = np.concatenate([best, h[keep]])
        best_i = np.concatenate([best_i, np.full(len(ys), ia)])
        best_j = np.concatenate([best_j, np.array([allv[y][1] for y in ys], dtype=np.int64)])
        if len(best) > 4 * N_PAIRS:
            o = np.argsort(best, kind="stable")[:N_PAIRS]
            best, best_i, best_j = best[o], best_i[o], best_j[o]
            cut = best[-1]
    o = np.argsort(best, kind="stable")[:N_PAIRS]
    return [(int(best[k]), int(best_i[k]), int(best_j[k])) for k in o]


def jaccards(items, pairs):
    """Exact set and weighted (sum of min / sum of max) Jaccard of each pair."""
    js, wjs = np.empty(len(pairs)), np.empty(len(pairs))
    for k, (i, j) in enumerate(pairs):
        (ka, ca), (kb, cb) = items[i], items[j]
        if not len(ka) and not len(kb):
            js[k] = wjs[k] = 0.0
            continue
        common, ia, ib = np.intersect1d(ka, kb, assume_unique=True, return_indices=True)
        js[k] = len(common) / (len(ka) + len(kb) - len(common))
        mn = int(np.minimum(ca[ia], cb[ib]).sum())
        wjs[k] = mn / (int(ca.sum()) + int(cb.sum()) - mn)
    return js, wjs


# ---------------------------------------------------------------- analysis

def boot_auc(j_rr, inst_rr, j_ru, inst_ru, n_inst):
    rng = np.random.default_rng(1)
    vals = np.empty(N_BOOT)
    for b in range(N_BOOT):
        w = np.bincount(rng.integers(0, n_inst, n_inst), minlength=n_inst)
        vals[b] = auc(j_rr, j_ru, w[inst_rr], w[inst_ru])
    return float(np.nanpercentile(vals, 0.5)), float(np.nanpercentile(vals, 99.5))


def best_threshold(pos, neg):
    """The J that maximises TPR - FPR for the rule J >= threshold."""
    best, best_t = -1.0, 0.0
    neg_s, pos_s = np.sort(neg), np.sort(pos)
    for t in np.unique(np.concatenate([pos, neg])):
        d = (len(pos_s) - np.searchsorted(pos_s, t, "left")) / len(pos_s) \
            - (len(neg_s) - np.searchsorted(neg_s, t, "left")) / len(neg_s)
        if d > best:
            best, best_t = d, float(t)
    return best_t, best


def fmt(x, d=3):
    return "n/a" if x is None or (isinstance(x, float) and np.isnan(x)) else f"{x:.{d}f}"


def main():
    instances = 1000
    if "--instances" in sys.argv:
        instances = int(sys.argv[sys.argv.index("--instances") + 1])
    parquet = Path(sys.argv[sys.argv.index("--parquet") + 1]) if "--parquet" in sys.argv else PARQUET
    start = time.time()
    self_check()
    print("mhe_explore: self-check passed", flush=True)
    if not parquet.exists():
        sys.exit(f"error: {parquet} is missing. Run scripts/minhash/data/get_trajectories.sh first.")
    facts = Counter()
    facts["patch_lines"] = []
    facts["patch_total_lines"] = []
    meta, items_all, first40 = load(instances, facts, parquet)
    n = len(meta)
    print(f"mhe_explore: {n} trajectories loaded in {time.time() - start:.0f} s", flush=True)
    related, hard, unrel, by_inst, by_repo = build_pairs(meta)
    print(f"mhe_explore: pairs built in {time.time() - start:.0f} s", flush=True)
    inst_index = {iid: k for k, iid in enumerate(by_inst)}
    rr = [p for p in related if meta[p[0]]["resolved"] and meta[p[1]]["resolved"]]
    ru = [p for p in related if meta[p[0]]["resolved"] != meta[p[1]]["resolved"]]
    uu = [p for p in related if not meta[p[0]]["resolved"] and not meta[p[1]]["resolved"]]
    classes = {"related": related, "hard negative": hard, "unrelated": unrel, "RR": rr, "RU": ru, "UU": uu}
    non_empty = [i for i, m in enumerate(meta) if not m["empty_patch"]]
    ne = set(non_empty)

    res, store = {}, {}
    for c in BEHAVIOUR + PATCH:
        items = items_all[c]
        cl = classes if c in BEHAVIOUR else {k: [p for p in v if p[0] in ne and p[1] in ne] for k, v in classes.items()}
        idx = range(n) if c in BEHAVIOUR else non_empty
        sizes = np.array([len(items[i][0]) for i in idx])
        js, wj = {}, {}
        for k, v in cl.items():
            js[k], wj[k] = jaccards(items, v)
        distinct = sum(len(items[i][0]) for i in idx)
        repeated = sum(int((items[i][1] > 1).sum()) for i in idx)
        lo, hi = boot_auc(js["RR"], np.array([inst_index[meta[i]["iid"]] for i, _ in cl["RR"]], dtype=int),
                          js["RU"], np.array([inst_index[meta[i]["iid"]] for i, _ in cl["RU"]], dtype=int), len(inst_index))
        a_rrru = auc(js["RR"], js["RU"])
        res[c] = {"q": np.quantile(sizes, [0, .1, .5, .9, 1]), "median": float(np.median(sizes)),
                  "mean": {k: float(v.mean()) if len(v) else float("nan") for k, v in js.items()},
                  "med": {k: float(np.median(v)) if len(v) else float("nan") for k, v in js.items()},
                  "auc_hn": auc(js["related"], js["hard negative"]), "auc_un": auc(js["related"], js["unrelated"]),
                  "repeat": repeated / distinct if distinct else 0.0,
                  "wauc_hn": auc(wj["related"], wj["hard negative"]),
                  "auc_rrru": a_rrru, "ci": (lo, hi),
                  "claim": a_rrru >= 0.6 and not (lo <= 0.5 <= hi),
                  "share": {k: [float(np.mean(js[k] >= lv)) for lv in J_LEVELS] for k in ("related", "hard negative")}}
        store[c] = (js, wj, cl)
        print(f"mhe_explore: {c}: AUC {res[c]['auc_hn']:.3f}, {time.time() - start:.0f} s", flush=True)

    # Q8 choice (owner correction of 24 Nov 2026), Q15 stop, Q9, Q13.
    def derive(c):
        """Q9 and Q13 for candidate c: (t, Q9 text, Q13 lines, counts mode)."""
        r = res[c]
        counts_mode = r["wauc_hn"] - r["auc_hn"] >= 0.03
        q9 = (f"Set AUC {r['auc_hn']:.3f}, weighted AUC {r['wauc_hn']:.3f}, difference {r['wauc_hn'] - r['auc_hn']:+.3f}. "
              + ("The difference is 0.03 or more, so the recommendation is **counts** with the occurrence-number "
                 "encoding (`x#n`)." if counts_mode else "The difference is less than 0.03, so the recommendation is **plain sets**."))
        js, wj, _ = store[c]
        pos, neg = (wj["related"], wj["hard negative"]) if counts_mode else (js["related"], js["hard negative"])
        raw_t, youden = best_threshold(pos, neg)
        t = round(round(raw_t / 0.05) * 0.05, 2)
        q13 = [f"Measure: {'weighted' if counts_mode else 'set'} Jaccard. The J that maximises TPR − FPR is "
               f"{raw_t:.4f} (TPR − FPR = {youden:.3f}). Rounded to the nearest 0.05: **t = {t:.2f}**."]
        if t < 0.2:
            q13.append("**Q13 stop:** t < 0.2, so no (b, r) and no k are given. The owner decides.")
            return t, q9, q13, counts_mode
        k = next((k for k in K_CHOICES if np.sqrt(t * (1 - t) / k) <= 0.05), None)
        q13.append("k: " + ", ".join(f"√(t(1−t)/{kk}) = {np.sqrt(t * (1 - t) / kk):.4f}" for kk in K_CHOICES)
                   + (f". The smallest with ≤ 0.05 is **k = {k}**." if k else ". None is ≤ 0.05, so no k is given."))
        if k:
            mids = [(b, rr_, (1 - 0.5 ** (1 / b)) ** (1 / rr_)) for b, rr_ in BR_TABLE[k]]
            ok = [x for x in mids if abs(x[2] - t) <= 0.05]
            q13.append("(b, r) of the MH-19 table for k = " + str(k) + ", with the S-curve midpoint: "
                       + ", ".join(f"({b}, {rr_}) {m:.3f}" for b, rr_, m in mids) + ".")
            if ok:
                b, rr_, m = min(ok, key=lambda x: abs(x[2] - t))
                q13.append(f"Within 0.05 of t: {', '.join(f'({x[0]}, {x[1]})' for x in ok)}. "
                           f"The closest to t: **(b, r) = ({b}, {rr_})**, midpoint {m:.3f}.")
            else:
                q13.append("No (b, r) of the table has its midpoint within 0.05 of t.")
        return t, q9, q13, counts_mode

    lines = []
    stop15 = not any(res[c]["auc_hn"] >= 0.7 for c in BEHAVIOUR)
    by_auc, near, order = rank({c: res[c]["auc_hn"] for c in BEHAVIOUR}, {c: res[c]["med"]["related"] for c in BEHAVIOUR})
    top = res[by_auc[0]]["auc_hn"]
    chosen, runner = (None, None) if stop15 else (order[0], order[1] if len(order) > 1 else None)
    lines.append("1. Rank by AUC (related vs hard negative): "
                 + ", ".join(f"{c} {res[c]['auc_hn']:.3f}" for c in by_auc) + ".")
    lines.append("2. No candidate is left out for its set size (owner correction of 24 Nov 2026). "
                 "The set sizes are in the table of section 2.")
    if stop15:
        lines.append("**Q15 global stop:** no behaviour candidate reaches an AUC of 0.7 (related vs hard negative). "
                     "No item is recommended.")
    else:
        lines.append(f"3. Best AUC {top:.3f}. Within 0.02 of it, with the median related J: "
                     + ", ".join(f"{c} {res[c]['med']['related']:.3f}" for c in near)
                     + f". The highest median related J is **{chosen}**.")
        lines.append(f"4. Runner-up (the next candidate by the same rule: the near set by median related J, "
                     f"then the others by AUC): **{runner}**.")
        lines.append("5. The outcome-class AUCs (Q14) are reported in the table, and they do not take part in the choice.")
    derived = {c: derive(c) for c in (chosen, runner, "action_text_w1", "action_text_news_w1") if c}
    # ------------------------------------------------------------ report
    R = []
    lic = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    R.append("<!--\n" + lic.strip("\n") + "\n-->\n")
    R.append("# MH-E item analysis\n")
    R.append("Written by `uv run scripts/minhash/analysis/mhe_explore.py`. The ticket is MH-E1. "
             "All pair statistics use exact Jaccard on the full message content of the pinned parquet file. "
             "The owner correction of 24 Nov 2026 applies: action text widths 1, 2, 3 and 5, the shares of pairs "
             "at J ≥ 0.3 / 0.5 / 0.7, the choice by median related J inside 0.02 AUC of the best, and the runner-up.\n")
    R.append("## 1. Sample\n")
    n_res = sum(m["resolved"] for m in meta)
    R.append(f"- Task instances: {len(by_inst)} (the first {instances} by `md5(instance_id)` with at least 1 resolved "
             f"and 1 unresolved trajectory). Repos: {len(by_repo)}.")
    R.append(f"- Trajectories: {n}. Resolved share: {n_res / n:.3f}.")
    R.append(f"- Empty patches (left out of the patch items): {n - len(non_empty)}.")
    R.append("- Pairs: " + ", ".join(f"{k} {len(v)}" for k, v in classes.items()) + ".\n")
    R.append("Facts of the ticket, checked on this sample:\n")
    pl = np.array(facts["patch_lines"])
    R.append(f"- Bash commands that start with `cd /workspace/<repo> &&` or `;`: {facts['bash_cd'] / facts['bash']:.3f} (ticket: 0.97).")
    R.append(f"- Bash commands whose first word after the prefix is `python` or `python3`: {facts['bash_python'] / facts['bash']:.3f} (ticket: 0.55 for `python`, 0.022 for `python3`).")
    R.append(f"- Bash replies with an exit code: {facts['bash_exit'] / facts['bash']:.4f} (ticket: 0.9963). "
             f"Replies that contain `[Command finished with exit code` and have it at the end: "
             f"{facts['bash_finished_end']} of {facts['bash_finished_any']}.")
    R.append(f"- Trajectories that repeat a bash command: {facts['traj_repeat_cmd'] / facts['traj']:.3f} (ticket: 0.977).")
    R.append(f"- Median lines of `model_patch`: {np.median(facts['patch_total_lines']):g} (ticket: 121). "
             f"Median changed lines: {np.median(pl):g}.\n")
    R.append("## 2. Candidates\n")
    R.append("AUC HN: related vs hard negative. AUC UN: related vs unrelated. wAUC HN: the same with weighted Jaccard. "
             "Repeat: the share of distinct (trajectory, item) pairs whose item occurs more than once in the trajectory. "
             "RR vs RU: AUC with its 99% bootstrap interval (2,000 resamples of task instances, `default_rng(1)`).\n")

    def table(cands):
        out = ["| Candidate | size min / p10 / p50 / p90 / max | AUC HN | AUC UN | Repeat | wAUC HN | AUC RR vs RU [99% CI] |",
               "|---|---|---|---|---|---|---|"]
        for c in cands:
            r = res[c]
            out.append(f"| {c} | {' / '.join(f'{v:g}' for v in r['q'])} | {r['auc_hn']:.3f} | {r['auc_un']:.3f} | "
                       f"{r['repeat']:.3f} | {r['wauc_hn']:.3f} | {r['auc_rrru']:.3f} [{fmt(r['ci'][0])}, {fmt(r['ci'][1])}] |")
        out.append("")
        out.append("| Candidate | " + " | ".join(f"J {k} mean / median" for k in classes) + " |")
        out.append("|---|" + "---|" * len(classes))
        for c in cands:
            out.append(f"| {c} | " + " | ".join(f"{fmt(res[c]['mean'][k])} / {fmt(res[c]['med'][k])}" for k in classes) + " |")
        out.append("")
        out.append("Share of pairs with set J at or above each level (deduplication and LSH need pairs above t):\n")
        out.append("| Candidate | " + " | ".join(f"{k} J ≥ {lv}" for k in ("related", "hard negative") for lv in J_LEVELS) + " |")
        out.append("|---|" + "---|" * (2 * len(J_LEVELS)))
        for c in cands:
            out.append(f"| {c} | " + " | ".join(fmt(x) for k in ("related", "hard negative") for x in res[c]["share"][k]) + " |")
        out.append("")
        out.append("Outcome claims (Q14): " + "; ".join(
            f"{c}: " + ("**an outcome difference is found**" if res[c]["claim"] else "no outcome difference is found with this item")
            for c in cands) + ".\n")
        return out

    R += table(BEHAVIOUR)
    R.append("## 3. Choice\n")
    R.append("Q8:\n")
    R += lines
    R.append("")
    a_, b_ = res["action_text_w1"], res["action_text_news_w1"]
    R.append("Space-split words against the 20 Newsgroups tokenizer (median related J, median hard-negative J, "
             f"AUC HN, t): action_text_w1 {a_['med']['related']:.3f}, {a_['med']['hard negative']:.3f}, "
             f"{a_['auc_hn']:.3f}, {derived['action_text_w1'][0]:.2f}; action_text_news_w1 {b_['med']['related']:.3f}, "
             f"{b_['med']['hard negative']:.3f}, {b_['auc_hn']:.3f}, {derived['action_text_news_w1'][0]:.2f}.\n")
    for label, c in (("Chosen", chosen), ("Runner-up", runner)):
        if c:
            t_c, q9, q13, _ = derived[c]
            R.append(f"### {label}: {c}\n")
            R.append(f"Q9: {q9}\n")
            R.append("Q13:\n")
            R += [f"- {x}" for x in q13]
            R.append("")
    if chosen:
        lines_t, _, _, use_w = derived[chosen]
        R.append("## 4. Human check\n")
        js, wj, cl = store[chosen]
        allp, seen = [], set()
        for k in ("related", "hard negative", "unrelated"):
            vals = wj[k] if use_w else js[k]
            for (i, j), v in zip(cl[k], vals):
                if (i, j) not in seen:
                    seen.add((i, j))
                    allp.append((float(v), k, i, j))
        top20 = sorted(allp, key=lambda x: (-x[0], meta[x[2]]["tid"], meta[x[3]]["tid"]))[:20]
        near10 = sorted(allp, key=lambda x: (abs(x[0] - lines_t), meta[x[2]]["tid"], meta[x[3]]["tid"]))[:10]
        R.append(f"Item: {chosen}. The 20 pairs with the highest J, then the 10 pairs nearest to t = {lines_t:.2f}, "
                 "from all sampled pairs. Items longer than 160 characters are cut, marked with …\n")
        for title, group in (("Highest J", top20), ("Nearest to t", near10)):
            R.append(f"### {title}\n")
            for v, k, i, j in group:
                a, b = meta[i], meta[j]
                R.append(f"<details><summary>J = {v:.3f} · {k} · {a['tid']} ({a['iid']}, resolved {a['resolved']}) · "
                         f"{b['tid']} ({b['iid']}, resolved {b['resolved']})</summary>\n")
                for m_, x in ((a, i), (b, j)):
                    R.append(f"First 40 items of {m_['tid']}:\n\n```text")
                    R += [s if len(s) <= 160 else s[:159] + "…" for s in (y.replace("\n", " ") for y in first40[chosen][x])]
                    R.append("```\n")
                R.append("</details>\n")
    R.append("## 5. Patch items (solution similarity)\n")
    R.append("These do not take part in the choice of the behaviour item. Trajectories with an empty patch are left out.\n")
    R += table(PATCH)
    R.append("## 6. Run\n")
    cpu = next((l.split(":", 1)[1].strip() for l in Path("/proc/cpuinfo").read_text().splitlines()
                if l.startswith("model name")), platform.processor()) if Path("/proc/cpuinfo").exists() else platform.processor()
    R.append(f"Run time: {time.time() - start:.0f} s. Machine: {cpu}, {os.cpu_count()} CPUs, {platform.system()} "
             f"{platform.machine()}, Python {platform.python_version()}, DuckDB {duckdb.__version__}, NumPy {np.__version__}.")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(R) + "\n")
    print(f"mhe_explore: wrote {OUT} in {time.time() - start:.0f} s")


if __name__ == "__main__":
    main()

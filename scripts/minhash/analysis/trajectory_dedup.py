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

# Trajectory deduplication study. Builds candidate item sets per trajectory of
# the MH-15 sample (1,000 task instances, 11,166 trajectories), computes exact
# Jaccard for ALL 62.3 million pairs, and reports the J distributions and the
# cluster quality of connected components at each threshold. Run from the repo root:
#   uv run scripts/minhash/analysis/trajectory_dedup.py [--jsonl PATH] [--parquet PATH] [--out PATH]
# It reads scripts/minhash/data/trajectories_1000.jsonl (get_trajectories.sh
# --instances 1000). That export cuts tool replies to 1,000 characters; the items
# here use only the tool-call arguments, which it keeps intact. From the pinned
# parquet file it reads the issue text (the truth for the clusters) and model_patch.
# It writes scripts/minhash/results/trajectory_dedup.md.

import argparse
import json
import re
import shlex
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np
from scipy import sparse
from scipy.sparse.csgraph import connected_components

HERE = Path(__file__).resolve().parent.parent
JSONL = HERE / "data" / "trajectories_1000.jsonl"
PARQUET = HERE / "data" / "trajectories.parquet"
OUT = HERE / "results" / "trajectory_dedup.md"
T_GRID = [round(0.2 + 0.05 * i, 2) for i in range(16)]  # 0.20 .. 0.95
EDGE_MIN = min(T_GRID)
DENSE_DF = 0.01  # columns in more than 1% of trajectories go through dense BLAS

ROOT_RE = re.compile(r"/workspace/[^/\s\"']+")
CD_RE = re.compile(r"^\s*cd\s+\.?/?\s*(&&|;)\s*")
CODE_RE = re.compile(r"\s-c\s+['\"]|<<")  # python -c '...', heredocs: the body is code, not an action
PATH_RE = re.compile(r"^[\w.\-]+(/[\w.\-*]*)+$|^[\w\-]+\.(py|pyi|cfg|toml|ini|txt|md|rst|json|yaml|yml|c|h|cpp|js|ts)$")
WORD_RE = re.compile(rb"[a-z0-9]+")


# ---------------------------------------------------------------- item rules

def words(text):
    """The word tokenizer of MH-E1: ASCII lower case, split on every non-alphanumeric byte."""
    return [t.decode() for t in WORD_RE.findall(text.encode().lower())]


def rel(path):
    """/workspace/<repo>/x and ./x become x; the repo root becomes '.'."""
    p = ROOT_RE.sub(".", path)
    p = re.sub(r"^\./", "", p)
    return p or "."


def short_command(cmd):
    """The bash command without the cd prefix, without code bodies, first line only."""
    c = CD_RE.sub("", ROOT_RE.sub(".", cmd))
    m = CODE_RE.search(c)
    if m:
        c = c[:m.start()] + " <code>"
    return re.sub(r"\s+", " ", c.split("\n", 1)[0]).strip()


def arg_tokens(cmd):
    """The arguments of each pipeline segment of a short command: no command names, flags or numbers."""
    try:
        toks = shlex.split(cmd)
    except ValueError:
        toks = cmd.split()
    out, first = [], True
    for t in toks:
        if t in ("|", "&&", "||", ";", "&") or t.endswith(";"):
            first = True
            continue
        if first:  # the command name (python, grep, find, ...)
            first = False
            continue
        if t.startswith("-") or t.isdigit() or t in ("<code>", "2>&1", "/dev/null") or t.startswith((">", "2>")):
            continue
        out.append(rel(t))
    return out


def featurize(acts):
    """The item lists of every candidate for one trajectory (acts: (tool, args) of each tool call)."""
    created = {rel(str(a.get("path", ""))) for fn, a in acts
               if fn == "str_replace_editor" and a.get("command") == "create"}
    all_words, lines, paths, args, edits, nocode_words, diff = [], [], [], [], [], [], []
    for fn, a in acts:
        for key, v in a.items():  # MH-E1 action_text_words_w1: every string argument
            if isinstance(v, str):
                all_words.extend(words(CD_RE.sub("", ROOT_RE.sub(".", v)) if key == "command" and fn == "execute_bash" else v))
        if fn == "execute_bash":
            c = short_command(str(a.get("command", "")))
            if c in ("", "C-c", "C-d"):
                continue
            masked = c
            for p in sorted(created, key=lambda x: (-len(x), x)):  # scratch files get a generic name; longest first, fixed order
                masked = re.sub(r"(?<![\w./-])(?:\./)?" + re.escape(p) + r"(?![\w/-])", "<scratch>", masked)
            lines.append("b:" + masked)
            for t in arg_tokens(c):
                if t in created or t == ".":
                    continue
                if PATH_RE.match(t):
                    paths.append("f:" + t.rstrip("/"))
                args.append("a:" + t)
            nocode_words.extend(words(c))
        elif fn == "str_replace_editor":
            sub, p = str(a.get("command", "")), rel(str(a.get("path", "")))
            scratch = p in created
            lines.append(f"e:{sub}:{'<scratch>' if scratch else p}")
            if scratch:
                continue
            paths.append("f:" + p.rstrip("/"))
            nocode_words.extend(words(p))
            if sub in ("str_replace", "insert"):
                old_new = {}
                for k in ("old_str", "new_str"):
                    old_new[k] = {ln.strip() for ln in str(a.get(k, "") or "").splitlines() if ln.strip()}
                    for ln in str(a.get(k, "") or "").splitlines():
                        ln = ln.strip()
                        if ln:
                            edits.append(f"{k[0]}:{p}:{ln}")
                            nocode_words.extend(words(ln))
                # the changed lines only: old lines that the edit removes, new lines that it adds
                diff += sorted(f"o:{p}:{ln}" for ln in old_new["old_str"] - old_new["new_str"])
                diff += sorted(f"n:{p}:{ln}" for ln in old_new["new_str"] - old_new["old_str"])
    out = {
        "words_w1": all_words,
        "lines": lines,
        "paths": paths,
        "args": args + paths,
        "edits": edits,
        "core": args + paths + edits,
        "nocode_words": nocode_words,
        "paths_edits": paths + edits,
        "diffedits": diff,
        "paths_diffedits": paths + diff,
        "opening": lines[:20],
    }
    return out


def self_check():
    assert rel("/workspace/o__r__1.0/a/b.py") == "a/b.py" and rel("./x.py") == "x.py" and rel("/workspace/o__r__1.0") == "."
    assert short_command("cd /workspace/o__r__1.0 && python -c \"\nimport x\nprint(1)\"") == "python <code>"
    assert short_command("cd /workspace/o__r__1.0 && cat > t.py << 'EOF'\nx\nEOF") == "cat > t.py <code>"
    assert short_command("cd /workspace/o__r__1.0 &&  grep -n  x a.py") == "grep -n x a.py"
    assert arg_tokens("grep -rn 'def foo' --include=*.py pints/ | head -20") == ["def foo", "pints/"]
    assert arg_tokens("python -m pytest tests/test_a.py::T::t -v") == ["pytest", "tests/test_a.py::T::t"]
    f = featurize([("str_replace_editor", {"command": "create", "path": "/workspace/r/rep.py", "file_text": "z"}),
                   ("execute_bash", {"command": "cd /workspace/r && python ./rep.py a/rep.py"}),
                   ("str_replace_editor", {"command": "str_replace", "path": "/workspace/r/m.py",
                                           "old_str": " a = 1\n", "new_str": "a = 2"})])
    assert f["lines"] == ["e:create:<scratch>", "b:python <scratch> a/rep.py", "e:str_replace:m.py"]
    assert f["diffedits"] == ["o:m.py:a = 1", "n:m.py:a = 2"]
    assert f["paths"] == ["f:a/rep.py", "f:m.py"] and f["args"] == ["a:a/rep.py", "f:a/rep.py", "f:m.py"] and f["edits"] == ["o:m.py:a = 1", "n:m.py:a = 2"]
    assert ari(np.array([0, 0, 1, 1]), np.array([5, 5, 7, 7])) == 1.0
    assert abs(ari(np.array([0, 0, 1, 1]), np.array([0, 1, 0, 1])) - (-0.5)) < 1e-12


# ---------------------------------------------------------------- exact Jaccard

def to_matrix(item_lists, df_max=None, df_min=1, stop=None):
    """Binary CSR (trajectories x distinct items). df_max drops items in more than that share of
    trajectories; df_min drops items in fewer than df_min trajectories (they can never match);
    stop is a set of items to drop (a stop list from a reference sample) in place of df_max."""
    vocab, rows, cols = {}, [], []
    for r, items in enumerate(item_lists):
        for x in sorted(set(items) - (stop or set())):  # sorted: the column order (and so the MinHash permutations) must not depend on the string hash seed
            cols.append(vocab.setdefault(x, len(vocab)))
            rows.append(r)
    n = len(item_lists)
    m = sparse.csr_matrix((np.ones(len(rows), np.float32), (rows, cols)), shape=(n, len(vocab)))
    df = np.asarray(m.sum(axis=0)).ravel()
    inv = {v: k for k, v in vocab.items()}
    if df_max is not None or df_min > 1:
        keep = np.nonzero((df <= (df_max if df_max is not None else 1.0) * n) & (df >= df_min))[0]
        m, df = m[:, keep].tocsr(), df[keep]
        inv = {i: inv[k] for i, k in enumerate(keep)}
    return m, df, inv


def all_pairs(m, df, edge_min, task, repo):
    """Exact J of every pair, by blocks of rows: frequent columns through dense BLAS, rare ones sparse.

    Returns the J of same-task pairs, the J of same-repo other-task pairs, the count / sum /
    0.01-bin histogram of other-repo J, the pairs with J >= edge_min, and the set sizes."""
    n = m.shape[0]
    size = np.asarray(m.sum(axis=1)).ravel()
    dense_cols = np.nonzero(df > DENSE_DF * n)[0]
    sparse_cols = np.nonzero(df <= DENSE_DF * n)[0]
    md = m[:, dense_cols].toarray() if len(dense_cols) else None
    ms = m[:, sparse_cols].tocsr()
    mst = ms.T.tocsc()
    bins = np.linspace(0, 1, 101)
    other = {"n": 0, "sum": 0.0, "hist": np.zeros(100, np.int64)}
    rel_j, hard_j, edges = [], [], []
    B = 512
    for s in range(0, n, B):
        e = min(n, s + B)
        inter = (ms[s:e] @ mst).toarray()
        if md is not None:
            inter += md[s:e] @ md.T
        union = size[s:e, None] + size[None, :] - inter
        with np.errstate(invalid="ignore", divide="ignore"):
            j = np.where(union > 0, inter / union, 0.0)
        ii, jj = np.triu_indices(e - s, 1, n - s)  # local row, local column; global column jj + s > global row
        gi, jj = ii + s, jj + s
        jv = j[ii, jj]
        same_t = task[gi] == task[jj]
        same_r = repo[gi] == repo[jj]
        rel_j.append(jv[same_t])
        hard_j.append(jv[same_r & ~same_t])
        o = jv[~same_r]
        other["n"] += len(o)
        other["sum"] += float(o.sum())
        other["hist"] += np.histogram(np.minimum(o, 1 - 1e-12), bins)[0]
        k = jv >= edge_min
        edges.append((gi[k].astype(np.int32), jj[k].astype(np.int32), jv[k].astype(np.float32)))
    ei = np.concatenate([x[0] for x in edges])
    ej = np.concatenate([x[1] for x in edges])
    ev = np.concatenate([x[2] for x in edges])
    return np.concatenate(rel_j), np.concatenate(hard_j), other, (ei, ej, ev), size


def minhash_estimates(m, edges, k, seed):
    """MinHash estimates (k random permutations of the items, numpy default_rng(seed)) for the given pairs."""
    n, v = m.shape
    rng = np.random.default_rng(seed)
    sig = np.empty((n, k), np.int64)
    empty = np.diff(m.indptr) == 0
    starts = np.minimum(m.indptr[:-1], max(len(m.indices) - 1, 0))
    for s in range(k):
        perm = rng.permutation(v)
        sig[:, s] = np.minimum.reduceat(perm[m.indices], starts) if len(m.indices) else 0
    sig[empty] = -1 - np.arange(n)[empty, None]  # an empty set matches nothing
    ei, ej, _ = edges
    est = np.empty(len(ei), np.float32)
    for a in range(0, len(ei), 20_000):
        b = a + 20_000
        est[a:b] = (sig[ei[a:b]] == sig[ej[a:b]]).mean(axis=1)
    return est


def ari(a, b):
    """Adjusted Rand index of two labelings."""
    _, a = np.unique(a, return_inverse=True)
    _, b = np.unique(b, return_inverse=True)
    c = sparse.coo_matrix((np.ones(len(a)), (a, b))).tocsr()
    comb = lambda x: x * (x - 1) / 2
    s_ij = comb(c.data).sum()
    s_a = comb(np.asarray(c.sum(axis=1)).ravel()).sum()
    s_b = comb(np.asarray(c.sum(axis=0)).ravel()).sum()
    exp = s_a * s_b / comb(len(a))
    mx = (s_a + s_b) / 2
    return float((s_ij - exp) / (mx - exp)) if mx != exp else 1.0


def clusters(n, edges, t, truth, est=None):
    """Connected components of the pairs with J >= t (or estimate >= t), against the truth labels."""
    ei, ej, ev = edges
    k = (ev if est is None else est) >= t
    g = sparse.coo_matrix((np.ones(k.sum()), (ei[k], ej[k])), shape=(n, n))
    nc, lab = connected_components(g, directed=False)
    pairs = Counter(zip(lab.tolist(), truth.tolist()))
    best = defaultdict(int)
    for (c, _), v in pairs.items():
        best[c] = max(best[c], v)
    comp_truth = Counter(c for c, _ in pairs)
    truth_comps = Counter(tk for _, tk in pairs)
    sizes = np.bincount(lab)
    return {"t": t, "kept": nc, "ari": ari(lab, truth), "purity": sum(best.values()) / n,
            "merged": sum(1 for v in comp_truth.values() if v > 1),
            "split": sum(1 for v in truth_comps.values() if v > 1),
            "singletons": int((sizes == 1).sum()), "largest": int(sizes.max()),
            "cross_edges": int((k & (truth[ei] != truth[ej])).sum())}, lab


# ---------------------------------------------------------------- data

def load(path):
    T = []
    with open(path) as f:
        for line in f:
            d = json.loads(line)
            acts = []
            for s in d["steps"]:
                if s["role"] != "assistant" or not s["fn"]:
                    continue
                try:
                    a = json.loads(s["args"] or "{}")
                except json.JSONDecodeError:
                    a = {}
                acts.append((s["fn"], a if isinstance(a, dict) else {}))
            T.append((d["trajectory_id"], d["instance_id"], int(d["resolved"]), acts))
    return T


def load_parquet(path, T):
    """The issue text of each task instance and the model_patch of each trajectory (read-only DuckDB query)."""
    import duckdb
    con = duckdb.connect()
    con.execute("CREATE TABLE ids AS SELECT unnest(?) AS trajectory_id", [[t[0] for t in T]])
    rows = con.execute(f"""
        SELECT trajectory_id, instance_id, model_patch, list_filter(trajectory, lambda m: m.role = 'user')[1].content
        FROM read_parquet('{path}') JOIN ids USING (trajectory_id)""").fetchall()
    patch, issue = {}, {}
    for tid, iid, mp, user in rows:
        patch[tid] = mp or ""
        m = re.search(r"<issue_description>(.*?)</issue_description>", user or "", re.S)
        issue.setdefault(iid, (m.group(1) if m else (user or "")).strip())
    return patch, issue


def patch_lines(patch):
    """MH-E1 patch_no_new: <file>:<sign><trimmed line> of each changed line, without new files."""
    out, cur, new = set(), None, False
    for line in patch.splitlines():
        if line.startswith("diff --git "):
            cur, new = line.split(" b/", 1)[-1], False
        elif line.startswith("new file mode"):
            new = True
        elif line.startswith(("+++", "---")):
            continue
        elif line[:1] in ("+", "-") and cur and not new and line[1:].strip():
            out.add(f"{cur}:{line[0]}{line[1:].strip()}")
    return out


# ---------------------------------------------------------------- report

def stats(x, levels=(0.3, 0.5, 0.7)):
    return [float(np.mean(x)), float(np.median(x))] + [float(np.mean(x >= v)) for v in levels]


def best_t(pos, neg):
    """The J that maximises TPR - FPR for J >= t, on the 0.05 grid."""
    return max(T_GRID, key=lambda t: np.mean(pos >= t) - np.mean(neg >= t))


def auc(pos, neg):
    neg = np.sort(neg)
    return float(np.mean((np.searchsorted(neg, pos, "left") + np.searchsorted(neg, pos, "right")) / 2 / len(neg)))


def label_of(name, dfm, dfmin):
    return name + ("" if dfm is None else f"_df{dfm * 100:g}") + ("" if dfmin == 1 else f"_min{dfmin}")


CANDS = [("words_w1", None, 1), ("words_w1", 0.5, 1), ("words_w1", 0.2, 1), ("words_w1", 0.05, 1),
         ("words_w1", 0.2, 2), ("words_w1", 0.02, 2),
         ("nocode_words", None, 1), ("nocode_words", 0.2, 2),
         ("lines", None, 1), ("lines", 0.2, 2), ("opening", None, 1), ("opening", 0.2, 2),
         ("paths", None, 1), ("paths", 0.2, 2), ("args", None, 1), ("args", 0.2, 2),
         ("edits", None, 1), ("edits", None, 2),
         ("paths_edits", None, 1), ("paths_edits", 0.2, 1), ("paths_edits", None, 2), ("paths_edits", 0.2, 2),
         ("diffedits", None, 2), ("paths_diffedits", 0.2, 2), ("core", None, 1), ("core", 0.2, 2)]
CHOSEN = [("words_w1", 0.2, 2), ("paths_edits", 0.2, 2)]  # the two items this study recommends
KEEP_T = 0.30  # the recommended t of CHOSEN[0] under MinHash noise (section 5)
BASE = ("words_w1", None, 1)  # MH-E1 action_text_words_w1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jsonl", default=str(JSONL))
    ap.add_argument("--parquet", default=str(PARQUET))
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()
    self_check()
    t0 = time.time()
    T = load(args.jsonl)
    patch, issue_text = load_parquet(args.parquet, T)
    names = [iid for _, iid, _, _ in T]
    task = np.unique(names, return_inverse=True)[1]
    repo = np.unique([iid.rsplit("-", 1)[0] for iid in names], return_inverse=True)[1]
    issue = np.unique([issue_text[iid] for iid in names], return_inverse=True)[1]
    resolved = np.array([r for _, _, r, _ in T])
    feats = [featurize(acts) for _, _, _, acts in T]
    print(f"loaded {len(T)} in {time.time() - t0:.0f} s", flush=True)
    W = []  # report lines
    groups = defaultdict(list)
    for iid in sorted(set(names)):
        groups[issue_text[iid]].append(iid)
    same_issue = sorted(g for g in groups.values() if len(g) > 1)
    by_task = defaultdict(list)
    for i, nm in enumerate(names):
        by_task[nm].append(i)
    n_rel = sum(len(v) * (len(v) - 1) // 2 for v in by_task.values())
    W += ["## 1. Sample", "",
          f"- Trajectories: {len(T)}. Task instances: {len(by_task)}. Repos: {repo.max() + 1}. Pairs: {len(T) * (len(T) - 1) // 2:,}; same task {n_rel:,}.",
          f"- Distinct issue texts: {issue.max() + 1}. Task instances that share one issue text: "
          + "; ".join(", ".join(g) for g in same_issue) + ". The cluster tables use the issue text as truth.", ""]

    results, keep = [], {}
    for name, dfm, dfmin in CANDS:
        t1 = time.time()
        m, df, inv = to_matrix([f[name] for f in feats], dfm, dfmin)
        edge_min = 0.35 if (name, dfm) in (("words_w1", None), ("words_w1", 0.5)) else EDGE_MIN  # memory: most pairs pass 0.2
        if (name, dfm, dfmin) in CHOSEN or (name, dfm, dfmin) == BASE:
            edge_min = min(edge_min, 0.1) if (name, dfm, dfmin) != BASE else edge_min
        rj, hj, other, edges, size = all_pairs(m, df, edge_min, task, repo)
        cl = [clusters(len(T), edges, t, issue)[0] for t in T_GRID if t >= max(edge_min, 0.2)]
        lab = label_of(name, dfm, dfmin)
        h = other["hist"]
        r = {"label": lab, "size": np.percentile(size, [50]).tolist() + [int((size == 0).sum())], "rel": stats(rj), "hard": stats(hj),
             "other": [other["sum"] / other["n"]] + [h[int(v * 100):].sum() / other["n"] for v in (0.3, 0.5, 0.7)],
             "t": best_t(rj, hj), "auc": auc(rj, hj), "clusters": cl,
             "near": [int(((edges[2] >= v) & (task[edges[0]] == task[edges[1]])).sum()) for v in (0.8, 0.9)]
                     + [int(((edges[2] >= v) & (task[edges[0]] != task[edges[1]])).sum()) for v in (0.8, 0.9)]}
        results.append(r)
        if (name, dfm, dfmin) in CHOSEN or (name, dfm, dfmin) == BASE:
            keep[(name, dfm, dfmin)] = (m, inv, edges, other, rj)
        print(lab, f"{time.time() - t1:.0f} s", flush=True)

    W += ["## 2. Candidates: exact J by pair class", "",
          "Same task: two trajectories of one task instance. Same repo: same repo, other task instance. Other repo: every other pair (exact, all pairs).",
          "AUC: same task against same repo. t: the 0.05-grid J that maximises TPR − FPR for same task against same repo.", "",
          "| Item | median size (empty) | same task mean / median | ≥ 0.3 / 0.5 / 0.7 | same repo mean | ≥ 0.3 / 0.5 | other repo mean | ≥ 0.3 | AUC | t |",
          "|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        W.append(f"| {r['label']} | {r['size'][0]:.0f} ({r['size'][1]}) | {r['rel'][0]:.3f} / {r['rel'][1]:.3f} | "
                 f"{r['rel'][2]:.3f} / {r['rel'][3]:.3f} / {r['rel'][4]:.3f} | {r['hard'][0]:.3f} | {r['hard'][2]:.4f} / {r['hard'][3]:.4f} | "
                 f"{r['other'][0]:.3f} | {r['other'][1]:.5f} | {r['auc']:.3f} | {r['t']:.2f} |")
    W += ["", "## 3. Clusters on exact J (connected components of the pairs with J ≥ t)", "",
          "Truth: the issue text (997 issues). Kept: components, that is the trajectories that deduplication keeps. Merged: components with more than one issue. "
          "Split: issues in more than one component. Cross pairs: pairs of different issues with J ≥ t. The row of each item is its t with the highest ARI.", "",
          "| Item | t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |", "|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        c = max(r["clusters"], key=lambda c: c["ari"])
        W.append(f"| {r['label']} | {c['t']:.2f} | {c['kept']} | {c['ari']:.3f} | {c['purity']:.3f} | {c['merged']} | {c['split']} | {c['singletons']} | {c['largest']} | {c['cross_edges']} |")
    for key in [BASE] + CHOSEN:
        r = next(x for x in results if x["label"] == label_of(*key))
        W += ["", f"### {r['label']} at each t", "", "| t | kept | ARI | purity | merged | split | singletons | largest | cross pairs |", "|---|---|---|---|---|---|---|---|---|"]
        for c in r["clusters"]:
            if c["t"] <= 0.6:
                W.append(f"| {c['t']:.2f} | {c['kept']} | {c['ari']:.3f} | {c['purity']:.3f} | {c['merged']} | {c['split']} | {c['singletons']} | {c['largest']} | {c['cross_edges']} |")

    W += ["", "## 4. Near-duplicates: pairs with very high exact J", "", "| Item | same task ≥ 0.8 | same task ≥ 0.9 | other task ≥ 0.8 | other task ≥ 0.9 |", "|---|---|---|---|---|"]
    for r in results:
        W.append(f"| {r['label']} | {r['near'][0]} | {r['near'][1]} | {r['near'][2]} | {r['near'][3]} |")

    # MinHash check: real permutation sketches of the chosen items; pairs below the stored floor are bounded by the binomial model.
    from scipy.stats import binom
    W += ["", "## 5. The same clusters from MinHash estimates", "",
          "Signatures: k random permutations of the items (numpy `default_rng`, seeds 1 to 3), estimate = share of equal slots, for every pair with exact J ≥ 0.10. "
          "Pairs below 0.10 are not estimated; the last column is their expected number of estimates ≥ t under the binomial model (the sum over the 0.01 bins of the count × P(Bin(k, J) ≥ tk)).", "",
          "| Item | k | seed | t | kept | ARI | purity | merged | split | largest | cross pairs | expected extra from J < 0.10 |", "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for key in CHOSEN:
        m, inv, edges, other, rj = keep[key]
        r = next(x for x in results if x["label"] == label_of(*key))
        t0_ = max(r["clusters"], key=lambda c: c["ari"])["t"]
        for k in (128, 256):
            for seed in (1, 2, 3):
                est = minhash_estimates(m, edges, k, seed)
                for t in (round(t0_ - 0.05, 2), t0_, round(t0_ + 0.05, 2)):
                    mids = (np.arange(10) + 0.5) / 100  # other-repo pairs only; same-repo pairs below 0.10 are left out of the bound
                    extra = float((other["hist"][:10] * binom.sf(np.ceil(t * k) - 1, k, mids)).sum())
                    c = clusters(len(T), edges, t, issue, est)[0]
                    W.append(f"| {label_of(*key)} | {k} | {seed} | {t:.2f} | {c['kept']} | {c['ari']:.3f} | {c['purity']:.3f} | {c['merged']} | {c['split']} | {c['largest']} | {c['cross_edges']} | {extra:.1f} |")

    # Stop list from a reference sample: the df_max rule computed on every second task instance (sorted ids) only.
    half = {iid for x, iid in enumerate(sorted(set(names))) if x % 2 == 0}
    ref = [i for i, nm in enumerate(names) if nm in half]
    W += ["", "## 5b. Stop list from half of the task instances", "",
          "The items above the 20% DF cut are counted on the 500 task instances with an even position in the sorted ids, then dropped from all trajectories. "
          "The df_min rule stays on the full corpus: it only removes items no other trajectory has. Clusters on exact J, against the issue text, at each item's t.", "",
          "| Item | stop list | t | kept | ARI | purity | merged | split | cross pairs |", "|---|---|---|---|---|---|---|---|---|"]
    for key in CHOSEN:
        r = next(x for x in results if x["label"] == label_of(*key))
        t = max(r["clusters"], key=lambda c: c["ari"])["t"]
        lists = [f[key[0]] for f in feats]
        dfr = Counter(x for i in ref for x in set(lists[i]))
        stop = {x for x, v in dfr.items() if v > key[1] * len(ref)}
        m2, df2, _ = to_matrix(lists, None, key[2], stop)
        _, _, _, edges2, _ = all_pairs(m2, df2, t, task, repo)
        c = clusters(len(T), edges2, t, issue)[0]
        c0 = next(x for x in r["clusters"] if x["t"] == t)
        for nm, cc in (("full corpus", c0), (f"half ({len(stop)} items)", c)):
            W.append(f"| {label_of(*key)} | {nm} | {t:.2f} | {cc['kept']} | {cc['ari']:.3f} | {cc['purity']:.3f} | {cc['merged']} | {cc['split']} | {cc['cross_edges']} |")

    # Solution level: does the item tell attempts with the same final patch from attempts with another patch?
    pl = [patch_lines(patch[tid]) for tid, _, _, _ in T]
    jp, pairs = [], []
    for ix in by_task.values():
        for x in range(len(ix)):
            for y in range(x + 1, len(ix)):
                a, b = pl[ix[x]], pl[ix[y]]
                jp.append(len(a & b) / len(a | b) if a | b else 1.0)
                pairs.append((ix[x], ix[y]))
    jp = np.array(jp)
    W += ["", "## 6. Solution level", "",
          f"Final patch lines (MH-E1 `patch_no_new`), same-task pairs: mean J {jp.mean():.3f}; share ≥ 0.5 {np.mean(jp >= 0.5):.3f}, ≥ 0.8 {np.mean(jp >= 0.8):.3f}, = 1 {np.mean(jp == 1):.3f}.", "",
          "| Item | mean J, same patch (patch J ≥ 0.8) | mean J, other patch (patch J < 0.2) | AUC |", "|---|---|---|---|"]
    for key in [BASE] + CHOSEN + [("edits", None, 2), ("diffedits", None, 2), ("paths_diffedits", 0.2, 2)]:
        m, _, _ = to_matrix([f[key[0]] for f in feats], key[1], key[2])
        cols = [set(m.indices[m.indptr[i]:m.indptr[i + 1]].tolist()) for i in range(m.shape[0])]
        ji = np.array([len(cols[a] & cols[b]) / len(cols[a] | cols[b]) if cols[a] | cols[b] else 0.0 for a, b in pairs])
        W.append(f"| {label_of(*key)} | {ji[jp >= 0.8].mean():.3f} | {ji[jp < 0.2].mean():.3f} | {auc(ji[jp >= 0.8], ji[jp < 0.2]):.3f} |")

    # What deduplication keeps, for the first chosen item at its t.
    key = CHOSEN[0]
    m, inv, edges, other, rj = keep[key]
    r = next(x for x in results if x["label"] == label_of(*key))
    c, labs = clusters(len(T), edges, KEEP_T, issue)
    comp = defaultdict(list)
    for i, l in enumerate(labs):
        comp[l].append(i)
    with_res = sum(1 for ix in comp.values() if resolved[ix].any())
    W += ["", f"## 7. What deduplication keeps ({label_of(*key)}, t = {c['t']:.2f})", "",
          f"- Components: {c['kept']} for {issue.max() + 1} issues. Sizes: " + ", ".join(f"{k}: {v}" for k, v in sorted(Counter(len(v) for v in comp.values()).items())) + ".",
          f"- Components with at least one resolved trajectory: {with_res} ({with_res / len(comp):.3f}). Keeping a resolved one where there is one keeps {with_res} resolved trajectories.",
          "- Merged components (issues joined by one component):"]
    for ix in sorted(comp.values(), key=len, reverse=True):
        ts = Counter(names[i] for i in ix)
        if len({issue[i] for i in ix}) > 1:
            W.append(f"  - {len(ix)}: " + ", ".join(f"{k} ({v})" for k, v in sorted(ts.items())))
    W += ["- The 10 pairs of different issues with the highest J:"]
    ei, ej, ev = edges
    cross = np.nonzero(issue[ei] != issue[ej])[0]
    for x in cross[np.argsort(-ev[cross], kind="stable")][:10]:
        W.append(f"  - J {ev[x]:.3f}: {names[ei[x]]} / {names[ej[x]]}")

    head = Path(__file__).read_text().split("\n# /// script")[0].replace("# ", "").replace("#", "")
    lic = "<!--\n" + head.strip() + "\n-->\n\n"
    text = (lic + "# Trajectory deduplication: candidate items on exact Jaccard\n\n"
            "Written by `uv run scripts/minhash/analysis/trajectory_dedup.py`. Exact Jaccard for all pairs of the MH-15 sample. "
            "The findings and the trace examples are in `scripts/minhash/analysis/trajectory_dedup_findings.md`.\n\n" + "\n".join(W) + "\n")
    Path(args.out).write_text(text)
    print(f"wrote {args.out} in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()

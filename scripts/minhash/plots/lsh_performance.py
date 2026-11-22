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
# dependencies = ["marimo", "pandas", "matplotlib", "numpy"]
# ///

# Plots of the minhash-lsh-performance profile (MH-20).
#   uv run plots/lsh_performance.py [<results dir>]   writes, for each tag, the SVG files and lsh_performance_<tag>.md
#   uvx marimo edit plots/lsh_performance.py           opens the notebook

import marimo

app = marimo.App()


@app.cell
def _():
    import sys
    from pathlib import Path

    import marimo as mo
    import matplotlib

    matplotlib.use("svg")
    import matplotlib.pyplot as plt
    import numpy as np
    import pandas as pd

    # The same bytes for the same TSV, so committed SVG files change only when the data does.
    plt.rcParams["svg.hashsalt"] = "lsh_performance"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    FIT_POINTS = 3  # the brute-force line c N^2 is fitted to the largest 3 N of each method
    FIT_END_N = 2**18  # and drawn up to here
    BRUTE = {"minhash_brute": "minhash", "theta_brute": "theta"}  # method -> the method of its sketch phase
    COLOURS = {"minhash_lsh 32x4": "#2a78d6", "minhash_lsh 16x8": "#1baf7a",
               "minhash_brute": "#eb6834", "theta_brute": "#e87ba4"}

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results/characterisation")

    def read_tsv(path):
        """The table, and the "# key=value" metadata lines as a dict. "NA" reads as NaN."""
        meta = {}
        for line in path.read_text().splitlines():
            if line.startswith("# ") and "=" in line:
                key, value = line[2:].split("=", 1)
                meta[key] = value
        return pd.read_csv(path, sep="\t", comment="#"), meta

    # tag -> (time table, quality table, metadata)
    runs = {}
    for time_path in sorted(results_dir.glob("lsh_performance_time_*.tsv")):
        tag = time_path.stem.removeprefix("lsh_performance_time_")
        time, meta = read_tsv(time_path)
        quality, _ = read_tsv(results_dir / f"lsh_performance_quality_{tag}.tsv")
        runs[tag] = (time, quality, meta)
    return BRUTE, COLOURS, FIT_END_N, FIT_POINTS, LICENCE, mo, np, plt, results_dir, runs


@app.cell
def _(BRUTE, FIT_END_N, FIT_POINTS, np):
    def phase(time, method, name, b=0, r=0):
        """MedianSec of one phase, indexed by N."""
        d = time[(time["Method"] == method) & (time["Phase"] == name) & (time["B"] == b) & (time["R"] == r)]
        return d.set_index("N")["MedianSec"]

    def label(method, b, r):
        return f"{method} {b}x{r}" if method == "minhash_lsh" else method

    def lsh_totals(time):
        """(b, r) -> the minhash_lsh total (minhash sketch + add + cluster), indexed by N."""
        sketch = phase(time, "minhash", "sketch")
        d = time[time["Method"] == "minhash_lsh"]
        return {(b, r): sketch.loc[phase(time, "minhash_lsh", "add", b, r).index]
                + phase(time, "minhash_lsh", "add", b, r) + phase(time, "minhash_lsh", "cluster", b, r)
                for b, r in sorted(set(zip(d["B"], d["R"])), reverse=True)}

    def brute_model(time, method):
        """The measured total (sketch + brute) indexed by N, and a function of N for the fitted total.
        The fitted total is c N^2, with c from least squares on the brute phase at the largest
        FIT_POINTS N, plus the sketch time: measured where it is, otherwise the time per document at
        the largest measured N times N."""
        sketch = phase(time, BRUTE[method], "sketch")
        brute = phase(time, method, "brute")
        top = brute.sort_index().iloc[-FIT_POINTS:]
        n2 = top.index.to_numpy(dtype=float) ** 2
        c = float(np.sum(top.to_numpy() * n2) / np.sum(n2 * n2))
        per_doc = sketch.iloc[-1] / sketch.index[-1]

        def fitted(n):
            return c * n * n + (sketch.loc[n] if n in sketch.index else per_doc * n)

        return sketch.loc[brute.index] + brute, fitted, c

    def fit_ns():
        return np.geomspace(2**8, FIT_END_N, 41)

    return brute_model, fit_ns, label, lsh_totals, phase


@app.cell
def _(LICENCE, mo, results_dir):
    def save_svg(fig, name):
        """Writes the figure to <results dir>/<name> with the licence header, when run as a script."""
        if mo.running_in_notebook():
            return fig
        path = results_dir / name
        fig.savefig(path, format="svg", metadata={"Date": None})
        head, rest = path.read_text().split("\n", 1)
        path.write_text(f"{head}\n<!--\n{LICENCE}\n-->\n{rest}")
        return fig

    return (save_svg,)


@app.cell
def _(BRUTE, COLOURS, brute_model, fit_ns, label, lsh_totals, plt, runs, save_svg):
    def _time_figure(tag, time, meta):
        fig, ax = plt.subplots(figsize=(6.4, 4.4))
        for (b, r), total in lsh_totals(time).items():
            name = label("minhash_lsh", b, r)
            ax.plot(total.index, total, "-", marker="o", markersize=3, color=COLOURS[name], label=name)
        for method in BRUTE:
            measured, fitted, _ = brute_model(time, method)
            ax.plot(measured.index, measured, "-", marker="o", markersize=3, color=COLOURS[method], label=method)
            ns = fit_ns()
            ax.plot(ns, [fitted(n) for n in ns], "--", color=COLOURS[method], linewidth=1,
                    label=f"{method}, fitted c N²")
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("N (documents)")
        ax.set_ylabel("total time (s), median")
        ax.set_title(f"All pairs with J ≥ {meta['threshold']}: {meta['compiler']}, {meta['cpu']}", fontsize=9)
        ax.legend(fontsize=7)
        fig.tight_layout()
        return save_svg(fig, f"lsh_performance_time_{tag}.svg")

    [_time_figure(tag, time, meta) for tag, (time, _, meta) in runs.items()]
    return


@app.cell
def _(COLOURS, label, plt, runs, save_svg):
    def _memory_figure(tag, quality, meta):
        fig, ax = plt.subplots(figsize=(6.4, 4))
        for (method, b, r), d in quality.groupby(["Method", "B", "R"], sort=False):
            name = label(method, b, r)
            ax.plot(d["N"], d["BytesPerDoc"], "-", marker="o", markersize=3, color=COLOURS[name], label=name)
        ax.set_xscale("log", base=2)
        ax.set_ylim(bottom=0)
        ax.set_xlabel("N (documents)")
        ax.set_ylabel("bytes per document")
        ax.set_title(f"Memory: {meta['compiler']}, {meta['cpu']}", fontsize=9)
        ax.legend(fontsize=7)
        fig.tight_layout()
        return save_svg(fig, f"lsh_performance_memory_{tag}.svg")

    [_memory_figure(tag, quality, meta) for tag, (_, quality, meta) in runs.items()]
    return


@app.cell
def _(COLOURS, label, plt, runs, save_svg):
    def _quality_figure(tag, quality, meta):
        fig, axes = plt.subplots(1, 2, figsize=(9, 3.8), sharey=True)
        for ax, column in zip(axes, ("Recall", "Precision")):
            for (method, b, r), d in quality.groupby(["Method", "B", "R"], sort=False):
                name = label(method, b, r)
                ax.plot(d["N"], d[column], "-", marker="o", markersize=3, color=COLOURS[name], label=name)
            ax.set_xscale("log", base=2)
            ax.set_ylim(0, 1.05)
            ax.set_title(column)
            ax.set_xlabel("N (documents)")
        axes[0].legend(fontsize=7)
        fig.suptitle(f"Quality at J ≥ {meta['threshold']}: {meta['compiler']}, {meta['cpu']}", fontsize=9)
        fig.tight_layout()
        return save_svg(fig, f"lsh_performance_quality_{tag}.svg")

    [_quality_figure(tag, quality, meta) for tag, (_, quality, meta) in runs.items()]
    return


@app.cell
def _(BRUTE, LICENCE, brute_model, label, lsh_totals, mo, phase, results_dir, runs):
    def _summary(tag, time, quality, meta):
        lsh = lsh_totals(time)
        max_n = max(t.index.max() for t in lsh.values())
        queries = int(meta["queries_per_class"])
        lines = [
            f"# LSH performance: {meta['compiler']}, {meta['cpu']}",
            "",
            f"Written by `plots/lsh_performance.py` from `lsh_performance_time_{tag}.tsv` and"
            f" `lsh_performance_quality_{tag}.tsv` (profile `{meta['profile']}`, MH-20).",
            f"Git commit `{meta['git_commit']}`, build flags `{meta['build_flags']}`.",
            f"Task: all pairs with J ≥ {meta['threshold']} among N documents, k = {meta['k']},"
            f" repeats = {meta['repeats']} (the time is the median).",
            "",
            "The totals are: `minhash_lsh` = MinHash sketch + add + cluster; `minhash_brute` = MinHash"
            " sketch + brute; `theta_brute` = theta sketch + brute. Beyond the measured N, a brute-force"
            " total is the fitted c N² plus the sketch time, extrapolated linearly.",
            "",
            "## Crossover and speed-up",
            "",
            f"| LSH (b, r) | brute force | c (s / N²) | smallest N where LSH is faster | speed-up at N = {max_n} |",
            "|---|---|---|---|---|",
        ]
        for (b, r), total in lsh.items():
            for method in BRUTE:
                measured, fitted, c = brute_model(time, method)
                faster = [n for n in total.index
                          if total[n] < (measured[n] if n in measured.index else fitted(n))]
                crossover = "none" if not faster else (f"≤ {faster[0]}" if faster[0] == total.index.min()
                                                        else str(faster[0]))
                lines.append(f"| ({b}, {r}) | {method} | {c:.3g} | {crossover} |"
                             f" {fitted(max_n) / total[max_n]:.3g} |")
        lines += [
            "",
            "## Median time of each phase at the largest measured N (s)",
            "",
            "| method | b | r | N | phase | median s | min s |",
            "|---|---|---|---|---|---|---|",
        ]
        for (method, b, r, name), d in time.groupby(["Method", "B", "R", "Phase"], sort=False):
            row = d.loc[d["N"].idxmax()]
            lines.append(f"| {method} | {b} | {r} | {row['N']} | {name} | {row['MedianSec']:.4g} |"
                         f" {row['MinSec']:.4g} |")
        lines += [
            "",
            f"## ns per query at N = {max_n}",
            "",
            f"Each class has {queries} queries at this N. A query is `query_candidates()`, then"
            " `jaccard()` ≥ t on each candidate other than the query.",
            "",
            "| b | r | planted | unrelated |",
            "|---|---|---|---|",
        ]
        for b, r in lsh:
            planted = phase(time, "minhash_lsh", "query_planted", b, r)[max_n] * 1e9 / queries
            unrelated = phase(time, "minhash_lsh", "query_unrelated", b, r)[max_n] * 1e9 / queries
            lines.append(f"| {b} | {r} | {planted:.4g} | {unrelated:.4g} |")
        lines += [
            "",
            "## Recall and precision at the largest N of each method",
            "",
            "| method | N | true pairs | reported pairs | recall | precision | candidate pairs | bytes per document |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for (method, b, r), d in quality.groupby(["Method", "B", "R"], sort=False):
            row = d.loc[d["N"].idxmax()]
            lines.append(f"| {label(method, b, r)} | {row['N']} | {row['TruePairs']} | {row['ReportedPairs']} |"
                         f" {row['Recall']:.4g} | {row['Precision']:.4g} | {row['CandidatePairs']} |"
                         f" {row['BytesPerDoc']:.5g} |")
        lines += [
            "",
            f"- [Total time against N](lsh_performance_time_{tag}.svg)",
            f"- [Memory against N](lsh_performance_memory_{tag}.svg)",
            f"- [Recall and precision against N](lsh_performance_quality_{tag}.svg)",
            "",
        ]
        text = "\n".join(lines)
        if not mo.running_in_notebook():
            (results_dir / f"lsh_performance_{tag}.md").write_text(f"<!--\n{LICENCE}\n-->\n\n{text}")
        return mo.md(text)

    mo.vstack([_summary(tag, *run) for tag, run in runs.items()])
    return


if __name__ == "__main__":
    app.run()

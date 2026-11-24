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
# dependencies = ["marimo", "pandas", "matplotlib"]
# ///

# The figures of the full-data LSH check (MH-14b on 20 Newsgroups, MH-14c on Reuters-21578). results/<stem>.md
# shows them. The stem is lsh_20news_full (the default) or lsh_reuters_full.
#   uv run plots/lsh_20news_full.py [<results dir> [<stem>]]   writes the 6 <stem>_*.svg files
#   uvx marimo edit plots/lsh_20news_full.py                    opens the notebook

import marimo

__generated_with = "0.25.2"
app = marimo.App()


@app.cell
def _():
    import sys
    from pathlib import Path

    import marimo as mo
    import matplotlib

    matplotlib.use("svg")
    import matplotlib.pyplot as plt
    import pandas as pd

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")

    args = [a for a in sys.argv[1:] if not a.startswith("-")] if not mo.running_in_notebook() else []
    results_dir = Path(args[0]) if args else Path("results")
    STEM = args[1] if len(args) > 1 else "lsh_20news_full"
    ITEMS = {"lsh_reuters_full": "documents"}.get(STEM, "posts")  # the word for the items in the figures

    # The same bytes for the same TSV, so the committed SVG file changes only when the data does.
    plt.rcParams["svg.hashsalt"] = STEM
    pairs = pd.read_csv(results_dir / f"{STEM}_pairs.tsv", sep="\t", comment="#")  # "NA" reads as NaN
    estimates = pd.read_csv(results_dir / f"{STEM}_estimates.tsv", sep="\t", comment="#")
    clusters = pd.read_csv(results_dir / f"{STEM}_clusters.tsv", sep="\t", comment="#")
    merges = pd.read_csv(results_dir / f"{STEM}_merges.tsv", sep="\t", comment="#")
    threshold = float(next(line for line in (results_dir / f"{STEM}_merges.tsv").read_text().splitlines()
                           if line.startswith("# threshold=")).split("=")[1])
    test_alpha = float(next(line for line in (results_dir / f"{STEM}_pairs.tsv").read_text().splitlines()
                            if line.startswith("# test_alpha=")).split("=")[1])
    return ITEMS, LICENCE, STEM, clusters, estimates, merges, mo, pairs, plt, results_dir, test_alpha, threshold


@app.cell
def _(LICENCE, mo, pairs, plt, results_dir):
    K_VALUES = sorted(pairs["K"].unique())
    ROWS = sorted(pairs["R"].unique())
    COLOURS = [plt.get_cmap("viridis")(i / max(len(ROWS) - 1, 1) * 0.9) for i in range(len(ROWS))]

    def save_svg(fig, name):
        """Writes the figure to <results dir>/<name> with the licence header, when run as a script."""
        if mo.running_in_notebook():
            return fig
        path = results_dir / name
        fig.savefig(path, format="svg", metadata={"Date": None})
        head, rest = path.read_text().split("\n", 1)
        path.write_text(f"{head}\n<!--\n{LICENCE}\n-->\n{rest}")
        plt.close(fig)
        return fig

    def pr_panels(table, title, label, name):
        """One panel per k: precision against recall as t goes from 0.3 to 0.9, one curve per (b, r)."""
        fig, axes = plt.subplots(2, 3, figsize=(13, 8))
        low = int(table["Precision"].min() * 10) / 10  # the same scale in every panel
        for ax, k in zip(axes.flat, K_VALUES):
            for colour, r in zip(COLOURS, ROWS):
                e = table[(table["K"] == k) & (table["R"] == r)].sort_values("T")
                ax.plot(e["Recall"], e["Precision"], "o-", color=colour, markersize=3, linewidth=1.2)
                for _, p in e.iloc[[0, -1]].iterrows():  # the ends of the curve: t = 0.3 and t = 0.9
                    ax.annotate(f"t={p['T']:.1f}", (p["Recall"], p["Precision"]), fontsize=6,
                                xytext=(3, 3), textcoords="offset points", color=colour)
            ax.axvline(0.95, color="#999999", linewidth=0.8, linestyle=":")
            ax.axhline(0.95, color="#999999", linewidth=0.8, linestyle=":")
            ax.set_xlim(0, 1.02)
            ax.set_ylim(low, 1.005)
            ax.set_title(f"k = {k}")
            ax.set_xlabel(f"{label} recall")
            ax.set_ylabel(f"{label} precision")
        key = axes.flat[len(K_VALUES)]
        key.axis("off")
        for colour, r in zip(COLOURS, ROWS):
            key.plot([], [], "o-", color=colour, label=f"r = {r} slots per band (b = k / {r})")
        key.legend(loc="upper left", fontsize=9, frameon=False)
        key.text(0, 0.35, "Each curve runs from t = 0.3 to t = 0.9.\nThe dotted lines mark 0.95: the top-right\n"
                 "corner is good.\nFew rows per band (r = 2) catch more pairs\n"
                 "(high recall). Many rows (r = 16) catch only\nvery similar pairs (high precision).",
                 fontsize=9, va="top", transform=key.transAxes)
        fig.suptitle(title)
        fig.tight_layout()
        return save_svg(fig, name)

    return COLOURS, K_VALUES, ROWS, pr_panels, save_svg


@app.cell
def _(STEM, pairs, pr_panels):
    pr_panels(pairs, "Pairs: precision against recall of the reported pairs (candidate and Ĵ ≥ t)", "pair",
              f"{STEM}_pr.svg")
    return


@app.cell
def _(K_VALUES, ROWS, STEM, pairs, plt, save_svg):
    def _heatmap():
        """Recall and precision of every grid point, as 2 tables of colour."""
        thresholds = sorted(pairs["T"].unique())
        labels = [f"k = {k}, {k // r} × {r}" for k in K_VALUES for r in ROWS]
        fig, axes = plt.subplots(1, 2, figsize=(13, 8.5), sharey=True)
        for ax, column in zip(axes, ["Recall", "Precision"]):
            grid = [[pairs[(pairs["K"] == k) & (pairs["R"] == r) & (pairs["T"] == t)][column].iloc[0]
                     for t in thresholds] for k in K_VALUES for r in ROWS]
            image = ax.imshow(grid, cmap="viridis", vmin=0, vmax=1, aspect="auto")
            for i, row in enumerate(grid):
                for j, value in enumerate(row):
                    ax.text(j, i, f"{value:.2f}", ha="center", va="center", fontsize=7,
                            color="black" if value > 0.6 else "white",
                            fontweight="bold" if value >= 0.95 else "normal")
            ax.set_xticks(range(len(thresholds)), [f"{t:.1f}" for t in thresholds])
            ax.set_yticks(range(len(labels)), labels, fontsize=8)
            for boundary in range(len(ROWS), len(labels), len(ROWS)):
                ax.axhline(boundary - 0.5, color="white", linewidth=2)
            ax.set_xlabel("threshold t")
            ax.set_title(f"pair {column.lower()} (bold: ≥ 0.95)")
        fig.colorbar(image, ax=axes, shrink=0.6)
        fig.suptitle("Pairs: recall and precision at every grid point (rows: k, b × r)")
        return save_svg(fig, f"{STEM}_heatmap.svg")

    _heatmap()
    return


@app.cell
def _(STEM, pairs, plt, save_svg, test_alpha):
    def _theory():
        """The measured counts against the theory, and every p-value against the pass line."""
        fig, (left, right) = plt.subplots(1, 2, figsize=(13, 5.5))
        for column, expected, colour, label in [("ReportedTrue", "ExpectedTrue", "#1f77b4", "reported true pairs"),
                                                ("ReportedFalse", "ExpectedFalse", "#d62728", "reported false pairs")]:
            shown = pairs[pairs[expected] > 0]
            left.loglog(shown[expected], shown[column].clip(lower=0.5), "o", markersize=4, alpha=0.7,
                        color=colour, label=label)
        top = max(pairs["ExpectedTrue"].max(), pairs["ReportedTrue"].max()) * 1.5
        line = [0.01, top]
        left.loglog(line, line, color="#555555", linewidth=1, label="measured = theory")
        left.set_xlabel("expected count: Σ P(reported | J)")
        left.set_ylabel("measured count (0 is drawn at 0.5)")
        left.set_title("Each grid point: measured count against theory")
        left.legend(fontsize=8)
        p_values = sorted(list(pairs["PRecall"]) + list(pairs["PPrecision"]))
        right.semilogy(range(1, len(p_values) + 1), p_values, "o", markersize=3, color="#2ca02c")
        right.axhline(test_alpha, color="#d62728", linewidth=1.2, label=f"FAIL below {test_alpha:.3g}")
        right.set_xlabel("test, from the smallest p-value to the largest")
        right.set_ylabel("exact two-sided p-value")
        right.set_ylim(min(test_alpha, min(p_values)) / 10, 2)
        right.set_title(f"All {len(p_values)} tests (2 at each grid point)")
        right.legend(fontsize=8, loc="lower right")
        fig.suptitle("Pair check: is the measured count what the joint probability predicts?")
        fig.tight_layout()
        return save_svg(fig, f"{STEM}_theory.svg")

    _theory()
    return


@app.cell
def _(STEM, estimates, plt, save_svg):
    def _estimates():
        """The error of Ĵ for each k, against the binomial theory."""
        fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.5))
        ticks = list(estimates["K"])
        left.loglog(estimates["K"], estimates["Rmse"], "o-", label="measured RMSE of Ĵ − J")
        left.loglog(estimates["K"], estimates["RmsTheorySd"], "s--", label="theory: RMS of √(J (1 − J) / k)")
        left.loglog(estimates["K"], estimates["MeanError"].abs(), "^:", label="|mean error| (bias)")
        left.set_xticks(ticks, [str(k) for k in ticks])
        left.set_xlabel("k")
        left.set_title("Error of the estimate")
        left.legend(fontsize=8)
        right.semilogx(estimates["K"], estimates["Rmse"] / estimates["RmsTheorySd"], "o-")
        right.axhline(1.0, color="#555555", linewidth=1)
        right.set_xticks(ticks, [str(k) for k in ticks])
        right.set_ylim(0.8, 1.2)
        right.set_xlabel("k")
        right.set_ylabel("RMSE / theory")
        right.set_title("Ratio to the theory (1 = exact agreement)")
        fig.suptitle("Estimate error on the pairs with exact J ≥ 0.1")
        fig.tight_layout()
        return save_svg(fig, f"{STEM}_estimates.svg")

    _estimates()
    return


@app.cell
def _(STEM, clusters, pr_panels):
    pr_panels(clusters, "Clusters: co-membership precision against recall of get_duplicate_clusters()", "cluster",
              f"{STEM}_clusters.svg")
    return


@app.cell
def _(ITEMS, STEM, merges, plt, save_svg, threshold):
    import math

    def _merges():
        """Each worst merge as a graph: posts coloured by true cluster, reported pairs as lines."""
        ids = sorted(merges["Merge"].unique())
        settings = list(dict.fromkeys(merges.loc[merges["Kind"] == "edge", "Setting"]))
        if not ids:
            return None
        fig, axes = plt.subplots(len(settings), len(ids), figsize=(3.2 * len(ids), 3.6 * len(settings)),
                                 squeeze=False)
        palette = plt.get_cmap("tab10")
        for col, m in enumerate(ids):
            nodes = merges[(merges["Merge"] == m) & (merges["Kind"] == "node")].sort_values(["TrueCluster", "Post"])
            names = list(nodes["Post"])
            # posts of one true cluster sit next to each other on the circle
            pos = {name: (math.cos(2 * math.pi * i / len(names)), math.sin(2 * math.pi * i / len(names)))
                   for i, name in enumerate(names)}
            colour_of = {c: palette(i % 10) for i, c in enumerate(dict.fromkeys(nodes["TrueCluster"]))}
            weakest = None
            for row, setting in enumerate(settings):
                ax = axes[row][col]
                edges = merges[(merges["Merge"] == m) & (merges["Kind"] == "edge") & (merges["Setting"] == setting)]
                for _, e in edges.iterrows():
                    (x1, y1), (x2, y2) = pos[e["Post"]], pos[e["Other"]]
                    false_link = float(e["ExactJ"]) < threshold
                    ax.plot([x1, x2], [y1, y2], color="#d62728" if false_link else "#999999",
                            linewidth=1.5 if false_link else 0.8, zorder=1)
                    if false_link:
                        ax.text((x1 + x2) / 2, (y1 + y2) / 2, f"{float(e['ExactJ']):.2f}", fontsize=7,
                                color="#d62728", ha="center", va="center",
                                bbox={"facecolor": "white", "edgecolor": "none", "pad": 0.5})
                if row == 0 and len(edges):
                    weakest = edges.loc[edges["ExactJ"].astype(float).idxmin()]
                if weakest is not None and ((edges["Post"] == weakest["Post"]) & (edges["Other"] == weakest["Other"])).any():
                    (x1, y1), (x2, y2) = pos[weakest["Post"]], pos[weakest["Other"]]
                    ax.plot([x1, x2], [y1, y2], color="#d62728", linewidth=3.5, zorder=1)
                for name in names:
                    ax.scatter(*pos[name], s=60, color=colour_of[nodes.loc[nodes["Post"] == name, "TrueCluster"].iloc[0]],
                               edgecolor="black", linewidth=0.5, zorder=2)
                false_links = int((edges["ExactJ"].astype(float) < threshold).sum())
                parts = nodes["TrueCluster"].nunique()
                ax.set_title(f"merge {m}: {len(names)} {ITEMS}, {parts} true clusters\n"
                             f"{len(edges)} reported pairs, {false_links} false" if row == 0 else
                             f"{len(edges)} reported pairs, {false_links} false", fontsize=8)
                ax.set_xlim(-1.3, 1.3)
                ax.set_ylim(-1.3, 1.3)
                ax.set_aspect("equal")
                ax.set_xticks([])
                ax.set_yticks([])
                if col == 0:
                    k, b, r = (part[1:] for part in setting.split("_"))
                    ax.set_ylabel(f"k = {k}, {b} × {r}", fontsize=9)
        fig.suptitle(f"The worst merges at t = {threshold:.1f}: dots are {ITEMS} (colour = true cluster), lines are "
                     "reported pairs (red = false link, J < t; thick red = weakest link)", fontsize=10)
        fig.tight_layout()
        return save_svg(fig, f"{STEM}_merges.svg")

    _merges()
    return


if __name__ == "__main__":
    app.run()

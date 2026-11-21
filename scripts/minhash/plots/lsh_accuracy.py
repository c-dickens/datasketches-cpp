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

# Plots of the minhash-lsh-accuracy profile (MH-19).
#   uv run plots/lsh_accuracy.py [<results dir>]   writes the SVG files and lsh_accuracy.md
#   uvx marimo edit plots/lsh_accuracy.py           opens the notebook

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
    import numpy as np
    import pandas as pd

    # The same bytes for the same TSV, so committed SVG files change only when the data does.
    plt.rcParams["svg.hashsalt"] = "lsh_accuracy"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    THRESHOLDS = ["0.5", "0.7", "0.9"]
    REPORTED_K = 128

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results/characterisation")
    df = pd.read_csv(results_dir / "lsh_accuracy.tsv", sep="\t", comment="#")  # "NA" reads as NaN
    k_values = sorted(df["K"].unique())
    return (
        LICENCE,
        REPORTED_K,
        THRESHOLDS,
        df,
        k_values,
        mo,
        np,
        plt,
        results_dir,
    )


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

    def colours(plt, n):
        """One colour for each b, in order: b is ordered, so a sequential map."""
        return [plt.get_cmap("viridis")(x) for x in [i / max(n - 1, 1) * 0.9 for i in range(n)]]

    return colours, save_svg


@app.cell
def _(colours, df, k_values, mo, plt, save_svg):
    def _scurve_figure(k):
        d = df[df["K"] == k]
        bands = sorted(d["B"].unique())
        fig, ax = plt.subplots(figsize=(8, 4.5))
        for colour, b in zip(colours(plt, len(bands)), bands):
            e = d[d["B"] == b]
            r = k // b
            ax.plot(e["TrueJ"], e["CandidateRate"], color=colour, linewidth=1.4, label=f"b = {b}, r = {r}")
            ax.plot(e["TrueJ"], e["TheoryRate"], "--", color=colour, linewidth=1)
        ax.set_xlabel("true J")
        ax.set_ylabel("candidate rate")
        ax.set_title(f"LSH S-curves at k = {k} (solid: measured, dashed: 1 − (1 − J^r)^b)")
        ax.legend(loc="upper left", fontsize=8)
        fig.tight_layout()
        return save_svg(fig, f"lsh_scurve_k{k}.svg")

    mo.vstack([_scurve_figure(_k) for _k in k_values])  # the last expression is the cell output
    return


@app.cell
def _(REPORTED_K, THRESHOLDS, colours, df, plt, save_svg):
    def _reported_figure():
        d = df[df["K"] == REPORTED_K]
        bands = sorted(d["B"].unique())
        fig, axes = plt.subplots(1, len(THRESHOLDS), figsize=(12, 3.8), sharey=True)
        for ax, t in zip(axes, THRESHOLDS):
            for colour, b in zip(colours(plt, len(bands)), bands):
                e = d[d["B"] == b]
                ax.plot(e["TrueJ"], e[f"ReportedRate_t{t}"], color=colour, linewidth=1.2,
                        label=f"b = {b}, r = {REPORTED_K // b}")
            ax.axvline(float(t), linestyle="--", color="#555555", linewidth=1)
            ax.set_title(f"t = {t}")
            ax.set_xlabel("true J")
        axes[0].set_ylabel("reported rate")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=4, fontsize=8)
        fig.tight_layout(rect=(0, 0.14, 1, 1))
        return save_svg(fig, f"lsh_reported_k{REPORTED_K}.svg")

    _reported_figure()
    return


@app.cell
def _(LICENCE, THRESHOLDS, df, k_values, mo, np, results_dir):
    def _rate_at(e, column, j):
        return e.loc[np.isclose(e["TrueJ"], j), column].iloc[0]

    def _j50(e, column):
        """The J where the rate first reaches 0.5, by linear interpolation between grid points."""
        j, rate = e["TrueJ"].to_numpy(), e[column].to_numpy()
        above = np.flatnonzero(rate >= 0.5)
        if above.size == 0:
            return None
        i = above[0]
        if i == 0:
            return j[0]
        return j[i - 1] + (0.5 - rate[i - 1]) * (j[i] - j[i - 1]) / (rate[i] - rate[i - 1])

    def _exact_p(x, n, p):
        """Two-sided exact binomial p-value of x candidates in n trials: 2 min(P(X ≤ x), P(X ≥ x)), at most 1."""
        log_fact = np.concatenate(([0.0], np.cumsum(np.log(np.arange(1, n + 1)))))
        i = np.arange(n + 1)
        pmf = np.exp(log_fact[n] - log_fact[i] - log_fact[n - i] + i * np.log(p) + (n - i) * np.log1p(-p))
        return min(1.0, 2 * min(pmf[: x + 1].sum(), pmf[x:].sum()))

    def _summary():
        lines = [
            "# LSH accuracy: the S-curve and the reported rate",
            "",
            "Written by `plots/lsh_accuracy.py` from `lsh_accuracy.tsv` (profile `minhash-lsh-accuracy`, MH-19).",
            "",
            "## Candidate rate against the theory",
            "",
            "Each row has n = Trials and p = 1 − (1 − J^R)^B, computed again here at full precision, because the TSV",
            "prints TheoryRate with 10 digits. The rows are in three groups:",
            "",
            "- normal rows: n p ≥ 20 and n (1 − p) ≥ 20. The z-score",
            "  CandidateZ = (CandidateRate − p) / √(p (1 − p) / n) is valid here.",
            "- small-count rows: 0 < p < 1, but n p < 20 or n (1 − p) < 20. The normal approximation fails,",
            "  so the table gives the smallest exact two-sided binomial p-value. With m rows, a value near 1/m is normal.",
            "- exact rows: p is 0 or 1 in double precision. It is exactly 0 or 1 only at J = 0 and J = 1. In the other",
            "  rows, the true p is within about 1e-16 of 0 or 1. These are the rows where the profile writes CandidateZ",
            "  as 0 or NA. NA rows are exact rows where the measured rate differs (expected: 0).",
            "",
            "The rows are not independent: every grid point uses the same trials, and each B is a prefix of the next.",
            "",
            "| k | rows | normal rows | max abs(CandidateZ) | small-count rows | smallest exact p | exact rows | NA rows |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for k in k_values:
            d = df[df["K"] == k]
            n = d["Trials"]
            p = 1 - (1 - np.power(d["TrueJ"], d["R"])) ** d["B"]  # as lsh_theory_rate(), not the printed value
            inner = (p > 0) & (p < 1)
            normal = inner & (n * p >= 20) & (n * (1 - p) >= 20)
            small = d[inner & ~normal]
            smallest_p = min(
                _exact_p(round(rate * trials), int(trials), theory)
                for rate, trials, theory in zip(small["CandidateRate"], small["Trials"], p[small.index])
            ) if len(small) else float("nan")
            lines.append(
                f"| {k} | {len(d)} | {normal.sum()} | {d.loc[normal, 'CandidateZ'].abs().max():.3f}"
                f" | {len(small)} | {smallest_p:.4g} | {(~inner).sum()} | {d['CandidateZ'].isna().sum()} |"
            )
        lines += [
            "",
            "## Guidance: choose (b, r) for a threshold t",
            "",
            "Reported = candidate and estimated J ≥ t. J50: the J where the reported rate first reaches 0.5.",
            "FalsePositiveRate: the reported rate at J = t − 0.1. MissRate: 1 − the reported rate at J = t + 0.1.",
            "",
            "| k | b | r | t | J50 | FalsePositiveRate | MissRate |",
            "|---|---|---|---|---|---|---|",
        ]
        for k in k_values:
            d = df[df["K"] == k]
            for b in sorted(d["B"].unique()):
                e = d[d["B"] == b].sort_values("TrueJ")
                for t in THRESHOLDS:
                    column = f"ReportedRate_t{t}"
                    j50 = _j50(e, column)
                    fp = _rate_at(e, column, float(t) - 0.1)
                    miss = 1 - _rate_at(e, column, float(t) + 0.1)
                    j50_text = "—" if j50 is None else f"{j50:.3f}"
                    lines.append(f"| {k} | {b} | {k // b} | {t} | {j50_text} | {fp:.4f} | {miss:.4f} |")
        lines += ["", "## Plots", ""]
        lines += [f"- [S-curves at k = {k}](lsh_scurve_k{k}.svg)" for k in k_values]
        lines += ["- [Reported rate at k = 128](lsh_reported_k128.svg)", ""]
        text = "\n".join(lines)
        if not mo.running_in_notebook():
            (results_dir / "lsh_accuracy.md").write_text(f"<!--\n{LICENCE}\n-->\n\n{text}")
        return mo.md(text)

    _summary()
    return


@app.cell
def _(df):
    df[(df.K == 128) & (df.B == 32)]                        # one S-curve, row by row
    df[df.CandidateZ.abs() > 2.5]                           # the outlier rows
    df[df.TheoryRate.between(0.2, 0.8)].CandidateZ.describe()   # z-scores where they are reliable
    return


@app.cell
def _():
    return


if __name__ == "__main__":
    app.run()

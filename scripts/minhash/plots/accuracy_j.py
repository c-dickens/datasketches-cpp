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

# Plots of the minhash-accuracy-j profile (MH-16).
#   uv run plots/accuracy_j.py [<results dir>]   writes the SVG files and accuracy_j.md
#   uvx marimo edit plots/accuracy_j.py           opens the notebook

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
    plt.rcParams["svg.hashsalt"] = "accuracy_j"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    COLOURS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"]  # one per k, in K_VALUES order
    QUANTILES = ["0", "0.00135", "0.02275", "0.15866", "0.5", "0.84134", "0.97725", "0.99865", "1"]
    NOMINAL = {1: 0.6827, 2: 0.9545, 3: 0.9973}

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results/characterisation")
    df = pd.read_csv(results_dir / "accuracy_j.tsv", sep="\t", comment="#")
    k_values = sorted(df["K"].unique())
    return COLOURS, LICENCE, NOMINAL, QUANTILES, df, k_values, mo, np, plt, results_dir


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
def _(QUANTILES, df, k_values, np, plt, save_svg):
    def _error_figure():
        fig, axes = plt.subplots(1, len(k_values), figsize=(3.2 * len(k_values), 3.4), sharex=True)
        axes = np.atleast_1d(axes)
        for ax, k in zip(axes, k_values):
            d = df[df["K"] == k]
            j = d["TrueJ"]
            for z, (lo, hi) in zip((3, 2, 1), ((1, 7), (2, 6), (3, 5))):
                ax.fill_between(j, d[f"AbsErr_Q{QUANTILES[lo]}"], d[f"AbsErr_Q{QUANTILES[hi]}"],
                                color="#2a78d6", alpha=0.15 + 0.15 * (3 - z), linewidth=0,
                                label=f"±{z}σ quantiles")
                theory = z * np.sqrt(j * (1 - j) / k)
                ax.plot(j, theory, "--", color="#555555", linewidth=1, label="theory ±zσ" if z == 1 else None)
                ax.plot(j, -theory, "--", color="#555555", linewidth=1)
            ax.plot(j, d["AbsErr_Q0.5"], color="black", linewidth=1.5, label="median")
            ax.axhline(0, color="#999999", linewidth=0.5)
            ax.set_title(f"k = {k}")
            ax.set_xlabel("true J")
        axes[0].set_ylabel("estimate − J")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=5, fontsize=8)
        fig.tight_layout(rect=(0, 0.08, 1, 1))
        return save_svg(fig, "accuracy_j_error.svg")

    _error_figure()
    return


@app.cell
def _(COLOURS, QUANTILES, df, k_values, plt, save_svg):
    def _normalised_figure():
        fig, ax = plt.subplots(figsize=(8, 4.5))
        for z in (-3, -2, -1, 0, 1, 2, 3):
            ax.axhline(z, color="#999999", linewidth=0.8 if z == 0 else 0.5)
        for colour, k in zip(COLOURS, k_values):
            d = df[df["K"] == k]
            for q in QUANTILES[1:-1]:
                ax.plot(d["TrueJ"], d[f"NormErr_Q{q}"], color=colour, linewidth=1.2,
                        label=f"k = {k}" if q == "0.5" else None)
        ax.set_xlabel("true J")
        ax.set_ylabel("(estimate − J) / √(J(1 − J)/k)")
        ax.set_title("Normalised error quantiles (−3σ … +3σ)")
        ax.legend(loc="upper right", fontsize=8)
        fig.tight_layout()
        return save_svg(fig, "accuracy_j_normalised.svg")

    _normalised_figure()
    return


@app.cell
def _(COLOURS, NOMINAL, df, k_values, plt, save_svg):
    def _coverage_figure():
        fig, axes = plt.subplots(1, 3, figsize=(12, 3.6), sharex=True)
        for ax, (s, nominal) in zip(axes, NOMINAL.items()):
            for colour, k in zip(COLOURS, k_values):
                d = df[df["K"] == k]
                ax.plot(d["TrueJ"], d[f"Cover{s}SD"], color=colour, linewidth=1.2, label=f"k = {k}")
            ax.axhline(nominal, linestyle="--", color="#555555", linewidth=1, label="nominal")
            ax.set_title(f"jaccard_bounds at {s} SD")
            ax.set_xlabel("true J")
        axes[0].set_ylabel("coverage")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=len(k_values) + 1, fontsize=8)
        fig.tight_layout(rect=(0, 0.08, 1, 1))
        return save_svg(fig, "accuracy_j_coverage.svg")

    _coverage_figure()
    return


@app.cell
def _(LICENCE, df, k_values, mo, results_dir):
    def _summary():
        lines = [
            "# MinHash accuracy against J",
            "",
            "Written by `plots/accuracy_j.py` from `accuracy_j.tsv` (profile `minhash-accuracy-j`, MH-16).",
            "",
            "| k | max abs(BiasOverSE) | min SDRatio | max SDRatio | min Cover1SD | min Cover2SD | min Cover3SD |",
            "|---|---|---|---|---|---|---|",
        ]
        for k in k_values:
            d = df[df["K"] == k]
            lines.append(
                f"| {k} | {d['BiasOverSE'].abs().max():.3f} | {d['SDRatio'].min():.4f} | {d['SDRatio'].max():.4f}"
                f" | {d['Cover1SD'].min():.4f} | {d['Cover2SD'].min():.4f} | {d['Cover3SD'].min():.4f} |"
            )
        lines += [
            "",
            "Nominal coverage: 0.6827 (1 SD), 0.9545 (2 SD), 0.9973 (3 SD).",
            "",
            "- [Absolute error against J](accuracy_j_error.svg)",
            "- [Normalised error against J](accuracy_j_normalised.svg)",
            "- [Coverage of jaccard_bounds against J](accuracy_j_coverage.svg)",
            "",
        ]
        text = "\n".join(lines)
        if not mo.running_in_notebook():
            (results_dir / "accuracy_j.md").write_text(f"<!--\n{LICENCE}\n-->\n\n{text}")
        return mo.md(text)

    _summary()
    return


if __name__ == "__main__":
    app.run()

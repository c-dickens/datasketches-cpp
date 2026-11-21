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

# Plots of the minhash-accuracy-n profile (MH-17).
#   uv run plots/accuracy_n.py [<results dir> [<stem>]]   reads <stem>.tsv (default accuracy_n), and
#                                                          writes the SVG files and <stem>.md
#   uvx marimo edit plots/accuracy_n.py           opens the notebook

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
    plt.rcParams["svg.hashsalt"] = "accuracy_n"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    COLOURS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"]  # one per k, in K_VALUES order
    SKETCHES = {"MH": ("MinHash", "#2a78d6"), "TH": ("theta", "#eb6834")}
    NOMINAL = {1: 0.6827, 2: 0.9545, 3: 0.9973}

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results/characterisation")
    stem = args[1] if len(args) > 1 and not mo.running_in_notebook() else "accuracy_n"
    tsv = results_dir / f"{stem}.tsv"
    df = pd.read_csv(tsv, sep="\t", comment="#")
    # accuracy_n_half writes "# theta_lg_k=log2(K)-1". accuracy_n has no such line.
    theta_lg_k = next((line.split("=", 1)[1].strip() for line in tsv.read_text().splitlines()
                       if line.startswith("# theta_lg_k=")), None)
    THETA_NOTE = f", theta lg_k = {theta_lg_k}" if theta_lg_k else ""
    k_values = sorted(df["K"].unique())
    target_js = sorted(df["TargetJ"].unique())
    return COLOURS, LICENCE, NOMINAL, SKETCHES, THETA_NOTE, df, k_values, mo, np, plt, results_dir, stem, target_js, theta_lg_k


@app.cell
def _(LICENCE, mo, np, plt, results_dir):
    def save_svg(fig, name):
        """Writes the figure to <results dir>/<name> with the licence header, when run as a script."""
        if mo.running_in_notebook():
            return fig
        path = results_dir / name
        fig.savefig(path, format="svg", metadata={"Date": None})
        head, rest = path.read_text().split("\n", 1)
        path.write_text(f"{head}\n<!--\n{LICENCE}\n-->\n{rest}")
        return fig

    def panels(n):
        """A figure with n panels side by side, on one shared log2 n axis."""
        fig, axes = plt.subplots(1, n, figsize=(3.2 * n, 3.4), sharex=True)
        return fig, np.atleast_1d(axes)

    return panels, save_svg


@app.cell
def _(SKETCHES, THETA_NOTE, df, k_values, panels, save_svg, stem, target_js):
    def _error_figure(target_j):
        fig, axes = panels(len(k_values))
        for ax, k in zip(axes, k_values):
            d = df[(df["K"] == k) & (df["TargetJ"] == target_j)]
            n = d["N"]
            for prefix, (name, colour) in SKETCHES.items():
                q = f"{prefix}_AbsErr_Q"
                ax.fill_between(n, d[q + "0.02275"], d[q + "0.97725"], color=colour, alpha=0.25, linewidth=0,
                                label=f"{name} ±2σ quantiles")
                ax.plot(n, d[q + "0.00135"], color=colour, linewidth=0.5, label=f"{name} ±3σ quantiles")
                ax.plot(n, d[q + "0.99865"], color=colour, linewidth=0.5)
                ax.plot(n, d[q + "0.5"], color=colour, linewidth=1.5, label=f"{name} median")
            ax.plot(n, 2 * d["TheorySD"], "--", color="black", linewidth=1, label="MinHash theory ±2σ")
            ax.plot(n, -2 * d["TheorySD"], "--", color="black", linewidth=1)
            ax.axvline(k, linestyle=":", color="#555555", linewidth=1, label="n = k")
            ax.set_xscale("log", base=2)
            ax.set_title(f"k = {k}")
            ax.set_xlabel("union size n")
        axes[0].set_ylabel("estimate − J")
        fig.suptitle(f"Absolute error against n, J = {target_j:g}{THETA_NOTE}")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=4, fontsize=8)
        fig.tight_layout(rect=(0, 0.14, 1, 1))
        return save_svg(fig, f"{stem}_error_j{round(target_j * 100)}.svg")

    for _j in target_js:
        _error_figure(_j)
    return


@app.cell
def _(COLOURS, NOMINAL, df, k_values, panels, save_svg, stem):
    def _coverage_figure():
        fig, axes = panels(3)
        d5 = df[df["TargetJ"] == 0.5]
        # Panel 1: both sketches at 2 SD. Panels 2 and 3: MinHash at 1 and 3 SD.
        for ax, (s, prefixes) in zip(axes, [(2, ["MH", "TH"]), (1, ["MH"]), (3, ["MH"])]):
            for colour, k in zip(COLOURS, k_values):
                d = d5[d5["K"] == k]
                for prefix in prefixes:
                    ax.plot(d["N"], d[f"{prefix}_Cover{s}SD"], color=colour, linewidth=1.2,
                            linestyle="-" if prefix == "MH" else "--",
                            label=f"k = {k}, " + ("MinHash" if prefix == "MH" else "theta"))
            ax.axhline(NOMINAL[s], linestyle=":", color="black", linewidth=1, label="nominal")
            ax.set_xscale("log", base=2)
            ax.set_title(f"{s} SD, " + ("MinHash and theta" if s == 2 else "MinHash"))
            ax.set_xlabel("union size n")
        axes[0].set_ylabel("coverage at J = 0.5")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=len(k_values) + 1, fontsize=8)
        fig.tight_layout(rect=(0, 0.16, 1, 1))
        return save_svg(fig, f"{stem}_coverage.svg")

    _coverage_figure()
    return


@app.cell
def _(df, k_values, panels, save_svg, stem):
    def _memory_figure():
        fig, axes = panels(len(k_values))
        d5 = df[df["TargetJ"] == 0.5]  # theta A does not depend on J
        for ax, k in zip(axes, k_values):
            d = d5[d5["K"] == k]
            ax.fill_between(d["N"], d["TH_BytesA_Min"] / d["MH_Bytes"], d["TH_BytesA_Max"] / d["MH_Bytes"],
                            color="#eb6834", alpha=0.4, linewidth=0, label="theta A bytes / MinHash bytes, min to max")
            ax.axhline(1, color="black", linewidth=1, label="ratio 1")
            ax.set_xscale("log", base=2)
            ax.set_title(f"k = {k}")
            ax.set_xlabel("union size n")
        axes[0].set_ylabel("memory ratio")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=2, fontsize=8)
        fig.tight_layout(rect=(0, 0.08, 1, 1))
        return save_svg(fig, f"{stem}_memory.svg")

    _memory_figure()
    return


@app.cell
def _(LICENCE, df, k_values, mo, results_dir, stem, target_js, theta_lg_k):
    def _spread(d, prefix):
        return d[f"{prefix}_AbsErr_Q0.97725"] - d[f"{prefix}_AbsErr_Q0.02275"]

    def _summary():
        lines = [
            "# MinHash accuracy against set size, with theta",
            "",
            f"Written by `plots/accuracy_n.py` from `{stem}.tsv` (profile `minhash-{stem.replace('_', '-')}`, MH-17)."
            + (f" Theta uses lg_k = {theta_lg_k}, where K is the MinHash k." if theta_lg_k else ""),
            "",
            "## End of the small-set region",
            "",
            "The region starts at the smallest n. It ends at the last n before the theta ±2σ spread"
            " (error Q0.97725 − Q0.02275) first becomes larger than the MinHash spread. Equal spreads"
            " stay in the region, because at small k the quantiles take few values and often tie."
            " \"> n\" means that the region does not end inside the n range.",
            "",
            "| k | " + " | ".join(f"J = {j:g}" for j in target_js) + " |",
            "|---|" + "---|" * len(target_js),
        ]
        for k in k_values:
            cells = []
            for j in target_js:
                d = df[(df["K"] == k) & (df["TargetJ"] == j)].sort_values("N")
                larger = (_spread(d, "TH") > _spread(d, "MH")).to_numpy()
                if not larger.any():
                    cells.append(f"> {d['N'].iloc[-1]}")
                else:
                    first = larger.argmax()
                    cells.append(str(d["N"].iloc[first - 1]) if first > 0 else "none")
            lines.append(f"| {k} | " + " | ".join(cells) + " |")
        lines += [
            "",
            "## Memory of theta A against MinHash",
            "",
            "TH_BytesA / MH_Bytes at the points where theta A is in estimation mode (retained < n).",
            "",
            "| k | min ratio | max ratio |",
            "|---|---|---|",
        ]
        for k in k_values:
            d = df[(df["K"] == k) & (df["TH_RetainedA_Max"] < df["N"])]
            if len(d):
                lo = (d["TH_BytesA_Min"] / d["MH_Bytes"]).min()
                hi = (d["TH_BytesA_Max"] / d["MH_Bytes"]).max()
                lines.append(f"| {k} | {lo:.4f} | {hi:.4f} |")
            else:
                lines.append(f"| {k} | none | none |")
        lines += [
            "",
            f"- [Absolute error against n, J = 0.1]({stem}_error_j10.svg)",
            f"- [Absolute error against n, J = 0.5]({stem}_error_j50.svg)",
            f"- [Absolute error against n, J = 0.9]({stem}_error_j90.svg)",
            f"- [Coverage of the bounds against n]({stem}_coverage.svg)",
            f"- [Memory of theta A against MinHash]({stem}_memory.svg)",
            "",
        ]
        text = "\n".join(lines)
        if not mo.running_in_notebook():
            (results_dir / f"{stem}.md").write_text(f"<!--\n{LICENCE}\n-->\n\n{text}")
        return mo.md(text)

    _summary()
    return


if __name__ == "__main__":
    app.run()

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

# The plot of the full-data LSH check on 20 Newsgroups (MH-14b).
#   uv run plots/lsh_20news_full.py [<results dir>]   writes lsh_20news_full.svg
#   uvx marimo edit plots/lsh_20news_full.py           opens the notebook

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

    # The same bytes for the same TSV, so the committed SVG file changes only when the data does.
    plt.rcParams["svg.hashsalt"] = "lsh_20news_full"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results")
    pairs = pd.read_csv(results_dir / "lsh_20news_full_pairs.tsv", sep="\t", comment="#")  # "NA" reads as NaN
    estimates = pd.read_csv(results_dir / "lsh_20news_full_estimates.tsv", sep="\t", comment="#")
    return LICENCE, estimates, mo, pairs, plt, results_dir


@app.cell
def _(LICENCE, estimates, mo, pairs, plt, results_dir):
    def _figure():
        k_values = sorted(pairs["K"].unique())
        rows = sorted(pairs["R"].unique())
        colours = [plt.get_cmap("viridis")(i / max(len(rows) - 1, 1) * 0.9) for i in range(len(rows))]
        fig, axes = plt.subplots(2, 3, figsize=(13, 8))
        for ax, k in zip(axes.flat, k_values):
            for colour, r in zip(colours, rows):
                e = pairs[(pairs["K"] == k) & (pairs["R"] == r)].sort_values("T")
                ax.plot(e["Recall"], e["Precision"], "o-", color=colour, markersize=3, linewidth=1.2,
                        label=f"b = {k // r}, r = {r}")
                for _, p in e.iloc[[0, -1]].iterrows():  # the ends of the curve: t = 0.3 and t = 0.9
                    ax.annotate(f"{p['T']:.1f}", (p["Recall"], p["Precision"]), fontsize=6,
                                xytext=(3, 3), textcoords="offset points", color=colour)
            ax.set_xlim(0, 1.02)
            ax.set_ylim(int(pairs["Precision"].min() * 10) / 10, 1.005)  # the same scale in every panel
            ax.set_title(f"k = {k}")
            ax.set_xlabel("pair recall")
            ax.set_ylabel("pair precision")
            ax.legend(loc="lower left", fontsize=7)
        ax = axes.flat[len(k_values)]
        ax.loglog(estimates["K"], estimates["Rmse"], "o-", label="RMSE of Ĵ − J")
        ax.loglog(estimates["K"], estimates["RmsTheorySd"], "s--", label="RMS of √(J (1 − J) / k)")
        ax.loglog(estimates["K"], estimates["MeanError"].abs(), "^:", label="|mean error|")
        ax.set_xticks(estimates["K"], [str(k) for k in estimates["K"]])
        ax.set_xlabel("k")
        ax.set_title("estimate error (pairs with J ≥ 0.1)")
        ax.legend(fontsize=7)
        fig.suptitle("20 Newsgroups, all pairs: precision against recall as t goes from 0.3 to 0.9")
        fig.tight_layout()
        if not mo.running_in_notebook():
            path = results_dir / "lsh_20news_full.svg"
            fig.savefig(path, format="svg", metadata={"Date": None})
            head, rest = path.read_text().split("\n", 1)
            path.write_text(f"{head}\n<!--\n{LICENCE}\n-->\n{rest}")
        return fig

    _figure()
    return


if __name__ == "__main__":
    app.run()

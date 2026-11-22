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

# Plots of the minhash-update-speed and minhash-call-speed profiles (MH-18).
#   uv run plots/speed.py [<results dir>]   writes, for each tag, the SVG files and speed_<tag>.md
#   uvx marimo edit plots/speed.py           opens the notebook

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
    import pandas as pd

    # The same bytes for the same TSV, so committed SVG files change only when the data does.
    plt.rcParams["svg.hashsalt"] = "speed"

    LICENCE = Path(__file__).read_text().split("\n\n", 1)[0].replace("# ", "").replace("#", "")
    COLOURS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"]  # one per k, in K_VALUES order
    STYLES = {"minhash": "-", "theta": "--"}

    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    results_dir = Path(args[0]) if args and not mo.running_in_notebook() else Path("results/characterisation")

    def read_tsv(path):
        """The table, and the "# key=value" metadata lines as a dict."""
        meta = {}
        for line in path.read_text().splitlines():
            if line.startswith("# ") and "=" in line:
                key, value = line[2:].split("=", 1)
                meta[key] = value
        return pd.read_csv(path, sep="\t", comment="#"), meta

    # tag -> (update table, call table, metadata)
    runs = {}
    for update_path in sorted(results_dir.glob("update_speed_*.tsv")):
        tag = update_path.stem.removeprefix("update_speed_")
        update, meta = read_tsv(update_path)
        call, _ = read_tsv(results_dir / f"call_speed_{tag}.tsv")
        runs[tag] = (update, call, meta)
    return COLOURS, LICENCE, STYLES, mo, plt, results_dir, runs


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
def _(COLOURS, STYLES, plt, runs, save_svg):
    def _update_figure(tag, df, meta):
        types = list(dict.fromkeys(df["ItemType"]))
        fig, axes = plt.subplots(1, len(types), figsize=(4.2 * len(types), 4), sharey=True)
        for ax, item_type in zip(axes, types):
            for colour, k in zip(COLOURS, sorted(df["K"].unique())):
                for sketch, style in STYLES.items():
                    d = df[(df["Sketch"] == sketch) & (df["K"] == k) & (df["ItemType"] == item_type)]
                    ax.plot(d["N"], d["MeanNsPerItem"], style, color=colour, linewidth=1.2,
                            label=f"{sketch} k = {k}")
            ax.set_xscale("log", base=2)
            ax.set_yscale("log")
            ax.set_title(item_type)
            ax.set_xlabel("n (items per sketch)")
        axes[0].set_ylabel("ns per item")
        fig.suptitle(f"Update speed: {meta['compiler']}, {meta['cpu']}")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=df["K"].nunique(), fontsize=8)
        fig.tight_layout(rect=(0, 0.14, 1, 1))
        return save_svg(fig, f"update_speed_{tag}.svg")

    [_update_figure(tag, update, meta) for tag, (update, _, meta) in runs.items()]
    return


@app.cell
def _(STYLES, plt, runs, save_svg):
    def _call_figure(tag, df, meta):
        operations = list(dict.fromkeys(df["Operation"]))
        fig, axes = plt.subplots(1, len(operations), figsize=(4.2 * len(operations), 3.6))
        for ax, operation in zip(axes, operations):
            for colour, (sketch, style) in zip(("#2a78d6", "#eb6834"), STYLES.items()):
                d = df[(df["Sketch"] == sketch) & (df["Operation"] == operation)]
                ax.plot(d["K"], d["MeanNsPerCall"], style, marker="o", color=colour, linewidth=1.2, label=sketch)
            ax.set_xscale("log", base=2)
            ax.set_yscale("log")
            ax.set_title(operation)
            ax.set_xlabel("k")
        axes[0].set_ylabel("ns per call")
        axes[0].legend(fontsize=8)
        fig.suptitle(f"Call speed: {meta['compiler']}, {meta['cpu']}")
        fig.tight_layout()
        return save_svg(fig, f"call_speed_{tag}.svg")

    [_call_figure(tag, call, meta) for tag, (_, call, meta) in runs.items()]
    return


@app.cell
def _(LICENCE, mo, results_dir, runs):
    def _summary(tag, update, call, meta):
        top = update[update["N"] == update["N"].max()]
        types = list(dict.fromkeys(update["ItemType"]))
        operations = list(dict.fromkeys(call["Operation"]))
        lines = [
            f"# MinHash speed: {meta['compiler']}, {meta['cpu']}",
            "",
            f"Written by `plots/speed.py` from `update_speed_{tag}.tsv` and `call_speed_{tag}.tsv`"
            " (profiles `minhash-update-speed` and `minhash-call-speed`, MH-18).",
            f"Git commit `{meta['git_commit']}`, build flags `{meta['build_flags']}`.",
            "",
            f"## Mean ns per item at n = {top['N'].iloc[0]}",
            "",
            "| sketch | k | " + " | ".join(types) + " |",
            "|---|---|" + "---|" * len(types),
        ]
        for (sketch, k), d in top.groupby(["Sketch", "K"], sort=False):
            values = d.set_index("ItemType")["MeanNsPerItem"]
            lines.append(f"| {sketch} | {k} | " + " | ".join(f"{values[t]:.4g}" for t in types) + " |")
        lines += [
            "",
            "## Mean ns per call",
            "",
            "| sketch | k | " + " | ".join(operations) + " |",
            "|---|---|" + "---|" * len(operations),
        ]
        for (sketch, k), d in call.groupby(["Sketch", "K"], sort=False):
            values = d.set_index("Operation")["MeanNsPerCall"]
            lines.append(f"| {sketch} | {k} | " + " | ".join(f"{values[o]:.4g}" for o in operations) + " |")
        lines += [
            "",
            f"- [Update speed against n](update_speed_{tag}.svg)",
            f"- [Call speed against k](call_speed_{tag}.svg)",
            "",
        ]
        text = "\n".join(lines)
        if not mo.running_in_notebook():
            (results_dir / f"speed_{tag}.md").write_text(f"<!--\n{LICENCE}\n-->\n\n{text}")
        return mo.md(text)

    mo.vstack([_summary(tag, *run) for tag, run in runs.items()])
    return


if __name__ == "__main__":
    app.run()

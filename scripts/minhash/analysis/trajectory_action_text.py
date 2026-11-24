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
# dependencies = ["duckdb"]
# ///

# MH-15b data bridge. Writes the action text of each trajectory of the MH-15 sample, one line per
# trajectory, for lsh_trajectories_full. The C++ program cuts the text into words with
# demo::words, so all three datasets use one tokenizer. Run from the repo root:
#   uv run scripts/minhash/analysis/trajectory_action_text.py [--instances N] [--parquet PATH] [--out DIR]
# It reads scripts/minhash/data/trajectories.parquet (or PATH) and writes
# DIR/action_text.tsv (default DIR: scripts/minhash/data/trajectories_action_text).
#
# The action text (MH-E decision 1, the item action_text_words_w1 of MH-E1): the string values of
# the JSON arguments of every tool call of the agent, in step order, with a leading
# `cd /workspace/<repo> &&` or `cd /workspace/<repo>;` removed from bash commands. Tool replies are
# not used. Each line is: trajectory_id, instance_id, repo, resolved, text, separated by tabs.
# Tabs, line breaks and carriage returns in the text become spaces. The word tokenizer splits on
# them anyway, so this changes no word.

import json
import re
import sys
from pathlib import Path

import duckdb

HERE = Path(__file__).resolve().parent.parent
UPLOADED_RE = re.compile(r"<uploaded_files>\s*(/workspace/[^\s<]+)")
SPACE_RE = re.compile(r"[\t\r\n]")


def strip_cd(cmd, root):
    """MH-E1 Q10: remove a leading `cd <root> &&` or `cd <root>;`."""
    m = re.match(r"\s*cd\s+" + re.escape(root) + r"/?\s*(&&|;)\s*", cmd)
    return cmd[m.end():] if m else cmd


def action_text(traj):
    """The action text of one trajectory, as one line."""
    root = None
    for m in traj[:3]:
        u = UPLOADED_RE.search(m["content"] or "") if m["role"] == "user" else None
        if u:
            root = u.group(1).rstrip("/")
            break
    root = root or "/workspace/<none>"
    parts = []
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
            for key, v in args.items():
                if isinstance(v, str):
                    parts.append(strip_cd(v, root) if key == "command" and fn == "execute_bash" else v)
    return SPACE_RE.sub(" ", " ".join(parts))


def self_check():
    root = "/workspace/o__r__1.0"
    assert strip_cd("cd /workspace/o__r__1.0 && python -m pytest", root) == "python -m pytest"
    assert strip_cd("cd /workspace/o__r__1.0; ls", root) == "ls"
    assert strip_cd("cd /tmp && ls", root) == "cd /tmp && ls"
    msg = lambda role, content="", calls=None: {"role": role, "content": content, "tool_calls": calls}
    call = lambda fn, args: {"function": {"name": fn, "arguments": json.dumps(args)}}
    traj = [msg("system"), msg("user", "<uploaded_files>\n/workspace/o__r__1.0\n</uploaded_files>"),
            msg("assistant", "", [call("execute_bash", {"command": "cd /workspace/o__r__1.0 && grep -n\tx a.py"}),
                                  call("str_replace_editor", {"command": "view", "path": "/w/a.py", "view_range": [1, 2]})]),
            msg("tool", "reply words are not used"),
            msg("assistant", "", [call("think", {"thought": "line\nbreak"})])]
    assert action_text(traj) == "grep -n x a.py view /w/a.py line break", action_text(traj)


def main():
    instances = int(sys.argv[sys.argv.index("--instances") + 1]) if "--instances" in sys.argv else 1000
    parquet = Path(sys.argv[sys.argv.index("--parquet") + 1]) if "--parquet" in sys.argv else HERE / "data" / "trajectories.parquet"
    out_dir = Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else HERE / "data" / "trajectories_action_text"
    self_check()
    if not parquet.exists():
        sys.exit(f"error: {parquet} is missing. Run scripts/minhash/data/get_trajectories.sh first.")
    con = duckdb.connect()
    con.execute("SET threads TO 4")
    src = f"read_parquet('{parquet}')"
    # The MH-15 sample of data/get_trajectories.sh and MH-E1, in the same order.
    rows = con.execute(f"""
        WITH chosen AS (
          SELECT instance_id FROM {src} GROUP BY instance_id
          HAVING bool_or(resolved = 1) AND bool_or(resolved = 0)
          ORDER BY md5(instance_id) LIMIT {instances})
        SELECT t.trajectory_id, t.instance_id, t.repo, t.resolved, t.trajectory
        FROM {src} AS t JOIN chosen USING (instance_id)
        ORDER BY md5(t.instance_id), t.trajectory_id""")
    out_dir.mkdir(parents=True, exist_ok=True)
    part = out_dir / "action_text.tsv.part"
    n = 0
    with part.open("w", encoding="utf-8", newline="\n") as out:
        while batch := rows.fetchmany(200):
            for tid, iid, repo, resolved, traj in batch:
                for field in (tid, iid, repo):
                    assert not SPACE_RE.search(field), field
                out.write(f"{tid}\t{iid}\t{repo}\t{int(resolved)}\t{action_text(traj)}\n")
                n += 1
    part.replace(out_dir / "action_text.tsv")
    print(f"trajectory_action_text: wrote {n} trajectories to {out_dir / 'action_text.tsv'}")


if __name__ == "__main__":
    main()

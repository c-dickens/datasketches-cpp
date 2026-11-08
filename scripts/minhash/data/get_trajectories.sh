#!/usr/bin/env bash
#
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
#
# Fetches the SWE-rebench OpenHands trajectories and writes N task instances of
# them as JSON lines to ./trajectories_<N>.jsonl. Git ignores the downloaded
# files.
#
#   data/get_trajectories.sh [--instances N]    (default N = 100)
#
# 1. Downloads trajectories.parquet (about 2 GB, one pinned revision) and checks
#    its SHA-256.
# 2. Downloads the DuckDB CLI v1.5.6 for this platform and checks its SHA-256.
# 3. Chooses the first N task instances, ordered by md5(instance_id), that have
#    at least 1 resolved and at least 1 unresolved trajectory. It stops with an
#    error if fewer than N task instances qualify.
# 4. Writes one JSON object per trajectory of those task instances:
#    trajectory_id, instance_id, resolved, exit_status and steps. The steps are
#    the messages from the 3rd message on, each as {role, fn, args, out}.
#
# Files that already exist are not downloaded again. The output file is always
# rewritten.

set -euo pipefail

cd "$(dirname "$0")"

PARQUET_URL="https://huggingface.co/datasets/nebius/SWE-rebench-openhands-trajectories/resolve/35455389ab51bf5e2306bfd436ef72d0f98bf882/trajectories.parquet"
PARQUET_SHA256="14048dd1fcd22ce094b6e85f8a38f223a9ef1327031aaaad052804870212efa1"
DUCKDB_VERSION="1.5.6"

usage() {
  echo "usage: $0 [--instances N]" >&2
  exit 2
}

instances=100
while [ $# -gt 0 ]; do
  case "$1" in
    --instances)
      [ $# -ge 2 ] || usage
      instances="$2"
      shift 2
      ;;
    --instances=*)
      instances="${1#--instances=}"
      shift
      ;;
    *)
      usage
      ;;
  esac
done
if ! [[ "$instances" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --instances needs a positive integer, got '$instances'." >&2
  exit 2
fi

sha256_of() {
  if command -v sha256sum > /dev/null; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

check_sha256() {  # file, expected
  local actual
  actual="$(sha256_of "$1")"
  if [ "$actual" != "$2" ]; then
    echo "error: SHA-256 of $1 is $actual, expected $2. Delete it and run again." >&2
    exit 1
  fi
}

download() {  # url, file
  echo "trajectories: downloading $1"
  if ! curl -fsSL --retry 3 -o "$2.part" "$1"; then
    rm -f "$2.part"
    echo "error: the download of $1 failed." >&2
    exit 1
  fi
  mv "$2.part" "$2"
}

# 1. The dataset.
parquet="trajectories.parquet"
[ -f "$parquet" ] || download "$PARQUET_URL" "$parquet"
check_sha256 "$parquet" "$PARQUET_SHA256"

# 2. The DuckDB CLI.
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    platform="linux-amd64"
    duckdb_sha256="6e89deac1ebbc36eed0291caf8b567b030c7b86ac35998f71854e22b3c5d5e2f"
    ;;
  Linux-aarch64 | Linux-arm64)
    platform="linux-arm64"
    duckdb_sha256="c544e92c9b7c31fc53c2139802cabd8e2d1b2b3e3f933117f31611239c1402db"
    ;;
  Darwin-*)
    platform="osx-universal"
    duckdb_sha256="80a80c68736bd7dea53e8b02447e9759db0c8596ea156476ba396c44f54c5810"
    ;;
  *)
    echo "error: no DuckDB CLI build for $(uname -s) $(uname -m)." >&2
    exit 1
    ;;
esac
duckdb_dir="duckdb-$DUCKDB_VERSION"
duckdb="$duckdb_dir/duckdb"
if [ ! -x "$duckdb" ]; then
  mkdir -p "$duckdb_dir"
  zip="$duckdb_dir/duckdb_cli-$platform.zip"
  [ -f "$zip" ] || download \
    "https://github.com/duckdb/duckdb/releases/download/v$DUCKDB_VERSION/duckdb_cli-$platform.zip" "$zip"
  check_sha256 "$zip" "$duckdb_sha256"
  unzip -o -q "$zip" duckdb -d "$duckdb_dir"
  chmod +x "$duckdb"
fi

# 3. The task instances with at least 1 resolved and 1 unresolved trajectory.
qualifying="
    SELECT instance_id
    FROM read_parquet('$parquet')
    GROUP BY instance_id
    HAVING bool_or(resolved = 1) AND bool_or(resolved = 0)"
available="$("$duckdb" -batch -bail -csv -noheader -c "SELECT count(*) FROM ($qualifying)")"
if [ "$available" -lt "$instances" ]; then
  echo "error: only $available task instances have both a resolved and an unresolved trajectory, fewer than --instances $instances." >&2
  exit 1
fi

# 4. Choose the task instances and write the JSON lines.
out="trajectories_$instances.jsonl"
echo "trajectories: writing $out ($instances task instances)"
rm -f "$out.part"
"$duckdb" -batch -bail -c "
COPY (
  WITH chosen AS ($qualifying
    ORDER BY md5(instance_id)
    LIMIT $instances
  )
  SELECT
    t.trajectory_id,
    t.instance_id,
    t.resolved,
    t.exit_status,
    list_transform(
      list_slice(t.trajectory, 3, len(t.trajectory)),
      lambda m: {
        'role': m.role,
        'fn': CASE WHEN m.role = 'tool' THEN m.name
                   ELSE m.tool_calls[1].function.name END,
        'args': m.tool_calls[1].function.arguments,
        'out': left(m.content, 1000)
      }
    ) AS steps
  FROM read_parquet('$parquet') AS t
  JOIN chosen USING (instance_id)
  ORDER BY md5(t.instance_id), t.trajectory_id
) TO '$out.part' (FORMAT json);
"
mv "$out.part" "$out"

lines="$(wc -l < "$out" | tr -d ' ')"
resolved="$(grep -c '"resolved":1' "$out" || true)"
echo "trajectories: $out has $lines trajectories, $resolved resolved."

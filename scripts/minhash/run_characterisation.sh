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
# Runs the characterisation profiles, and plots their results.
#   ./run_characterisation.sh         the full profiles, into results/characterisation/
#   ./run_characterisation.sh smoke   the smoke profiles, into results/characterisation/smoke/
# Later tickets add their profiles here.

set -euo pipefail

cd "$(dirname "$0")"

case "${1:-}" in
  "") suffix=""; out="results/characterisation" ;;
  smoke) suffix="-smoke"; out="results/characterisation/smoke" ;;
  *) echo "usage: $0 [smoke]" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo "usage: $0 [smoke]" >&2; exit 2; }

jobs="${JOBS:-4}"

# Not build/: run_demos.sh configures that one with BUILD_TESTING=OFF.
cmake -S . -B build-characterise -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF > /dev/null
cmake --build build-characterise -j "$jobs" --target minhash_characterise > /dev/null

mkdir -p "$out"

# run_profile <profile name> <output file name>
run_profile() {
  local start=$SECONDS
  echo "== $1 =="
  build-characterise/minhash_characterise "$1" > "$out/$2"
  echo "$1: $((SECONDS - start)) s"
}

run_profile "minhash-accuracy-j$suffix" accuracy_j.tsv
uv run plots/accuracy_j.py "$out"
run_profile "minhash-accuracy-n$suffix" accuracy_n.tsv
uv run plots/accuracy_n.py "$out"
run_profile "minhash-accuracy-n-half$suffix" accuracy_n_half.tsv
uv run plots/accuracy_n.py "$out" accuracy_n_half
run_profile "minhash-lsh-accuracy$suffix" lsh_accuracy.tsv
uv run plots/lsh_accuracy.py "$out"

# run_speed <profile name> <file stem>: the speed results describe one machine, so the file
# name is <stem>_<tag>.tsv, with tag = <compiler>_<cpu> in lower case and runs of other
# characters replaced by one "_".
run_speed() {
  run_profile "$1" "$2.tsv.tmp"
  local tag
  tag=$(printf '%s_%s' "$(sed -n 's/^# compiler=//p' "$out/$2.tsv.tmp")" "$(sed -n 's/^# cpu=//p' "$out/$2.tsv.tmp")" \
    | LC_ALL=C tr '[:upper:]' '[:lower:]' | LC_ALL=C tr -cs 'a-z0-9' '_')
  mv "$out/$2.tsv.tmp" "$out/$2_$tag.tsv"
}

run_speed "minhash-update-speed$suffix" update_speed
run_speed "minhash-call-speed$suffix" call_speed
uv run plots/speed.py "$out"

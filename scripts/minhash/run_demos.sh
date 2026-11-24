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
# The one command that runs every MinHash demo: it gets the data, builds the
# demos, and runs them. Later tickets add their demos here.
# FULL=1 also runs the full-data LSH check (MH-14b), which takes about 15 minutes
# without its exact-pair cache.

set -euo pipefail

cd "$(dirname "$0")"

jobs="${JOBS:-4}"

data/get_20news.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF > /dev/null
cmake --build build -j "$jobs" > /dev/null

echo "== 20 Newsgroups: data check =="
build/news20_stats data/20_newsgroups

echo "== 20 Newsgroups: MinHash correctness =="
mkdir -p results
build/minhash_20news data/20_newsgroups results/minhash_20news.md

echo "== 20 Newsgroups: LSH correctness =="
build/lsh_20news data/20_newsgroups results/lsh_20news.md

if [ "${FULL:-0}" = 1 ]; then
  echo "== 20 Newsgroups: full-data LSH check =="
  cache=data/lsh_20news_full_exact_pairs.tsv
  packed=results/lsh_20news_full_exact_pairs.tsv.gz
  # Unpack the committed cache when data/ has none, or one with another header (its first 6 lines),
  # for example from an older revision. The program runs the exact pass only if neither matches.
  if [ -f "$packed" ] && ! cmp -s <(head -n 6 "$cache" 2>/dev/null) <(gunzip -c "$packed" | head -n 6); then
    gunzip -c "$packed" > "$cache.part" && mv "$cache.part" "$cache"
  fi
  build/lsh_20news_full data/20_newsgroups results
  gzip -9nc "$cache" > "$packed"
  uv run plots/lsh_20news_full.py results
fi
